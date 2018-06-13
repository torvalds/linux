/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-06-13
 */
	.arch armv7-a
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.eabi_attribute 18, 4
	.file	"rk_sftl.c"
	.text
.Ltext0:
	.global	__aeabi_uidiv
	.align	1
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	l2p_addr_tran, %function
l2p_addr_tran:
	.fnstart
.LFB271:
	.file 1 "drivers/rkflash/rksftl/sftl_flash.c"
	.loc 1 18 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL0:
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
.LCFI0:
	.loc 1 18 0
	mov	r8, r1
	.loc 1 25 0
	ldr	r3, .L3
	.loc 1 18 0
	mov	r9, r2
	.loc 1 32 0
	ldr	r6, [r0, #4]
.LVL1:
	.loc 1 25 0
	ldrh	r4, [r3, #8]
.LVL2:
	.loc 1 26 0
	ldrh	r5, [r3, #10]
.LVL3:
	.loc 1 27 0
	ldrh	r3, [r3, #14]
	.loc 1 33 0
	lsrs	r7, r6, #10
	.loc 1 37 0
	ubfx	r6, r6, #0, #10
.LVL4:
	.loc 1 27 0
	cmp	r3, #4
	.loc 1 34 0
	uxth	r0, r7
.LVL5:
	.loc 1 28 0
	itt	eq
	lsreq	r4, r4, #1
	.loc 1 29 0
	lsleq	r5, r5, #1
.LVL6:
	.loc 1 35 0
	uxth	r7, r7
	.loc 1 34 0
	mov	r1, r4
.LVL7:
	.loc 1 29 0
	it	eq
	uxtheq	r5, r5
.LVL8:
	.loc 1 34 0
	bl	__aeabi_uidiv
.LVL9:
	uxth	r0, r0
.LVL10:
	.loc 1 35 0
	mls	r4, r0, r4, r7
.LVL11:
	.loc 1 37 0
	mla	r4, r5, r4, r6
.LVL12:
	str	r4, [r8]
	.loc 1 38 0
	str	r0, [r9]
	.loc 1 41 0
	movs	r0, #0
.LVL13:
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.LVL14:
.L4:
	.align	2
.L3:
	.word	g_nand_phy_info
.LFE271:
	.fnend
	.size	l2p_addr_tran, .-l2p_addr_tran
	.align	1
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_set_blk_mode.part.6, %function
ftl_set_blk_mode.part.6:
	.fnstart
.LFB286:
	.file 2 "drivers/rkflash/rksftl/sftl_sys.c"
	.loc 2 1346 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL15:
	.loc 2 1349 0
	ldr	r3, .L6
	lsrs	r1, r0, #5
	and	r0, r0, #31
.LVL16:
	ldr	r2, [r3]
	movs	r3, #1
	lsl	r0, r3, r0
	ldr	r3, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	bx	lr
.L7:
	.align	2
.L6:
	.word	.LANCHOR0
.LFE286:
	.fnend
	.size	ftl_set_blk_mode.part.6, .-ftl_set_blk_mode.part.6
	.align	1
	.global	Ftl_log2
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
.LFB152:
	.file 3 "drivers/rkflash/rksftl/sftl_plat.c"
	.loc 3 128 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL17:
	.loc 3 132 0
	movs	r1, #0
	.loc 3 130 0
	movs	r2, #1
.LVL18:
.L9:
	.loc 3 132 0 discriminator 1
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L10
	.loc 3 134 0
	subs	r0, r3, #1
.LVL19:
	.loc 3 136 0
	uxth	r0, r0
	bx	lr
.LVL20:
.L10:
	.loc 3 133 0 discriminator 3
	lsls	r2, r2, #1
	b	.L9
.LFE152:
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	1
	.global	FtlPrintInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPrintInfo, %function
FtlPrintInfo:
	.fnstart
.LFB153:
	.loc 3 159 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL21:
	bx	lr
.LFE153:
	.fnend
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	1
	.global	FtlSysBlkNumInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
.LFB154:
	.loc 3 165 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL22:
	.loc 3 168 0
	ldr	r3, .L13
	cmp	r0, #24
	it	cc
	movcc	r0, #24
	.loc 3 169 0
	ldrh	r2, [r3, #8]
	.loc 3 170 0
	ldrh	r1, [r3, #18]
	.loc 3 168 0
	str	r0, [r3, #4]
	.loc 3 169 0
	muls	r2, r0, r2
	.loc 3 170 0
	subs	r0, r1, r0
.LVL23:
	.loc 3 171 0
	ldr	r1, [r3, #24]
	.loc 3 170 0
	strh	r0, [r3, #16]	@ movhi
	.loc 3 174 0
	movs	r0, #0
	.loc 3 169 0
	str	r2, [r3, #12]
	.loc 3 171 0
	subs	r2, r1, r2
	str	r2, [r3, #20]
	.loc 3 174 0
	bx	lr
.L14:
	.align	2
.L13:
	.word	.LANCHOR0
.LFE154:
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
	.align	1
	.global	FtlConstantsInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
.LFB155:
	.loc 3 183 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL24:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI1:
	.loc 3 183 0
	mov	fp, r0
	.loc 3 191 0
	ldrh	r3, [fp, #14]
	.loc 3 188 0
	ldrh	r2, [r0]
	ldr	r4, .L25
	.loc 3 191 0
	cmp	r3, #4
	.loc 3 189 0
	ldrh	r6, [r0, #2]
	.loc 3 190 0
	ldrh	r0, [r0, #4]
.LVL25:
	.loc 3 188 0
	strh	r2, [r4, #28]	@ movhi
	.loc 3 189 0
	strh	r6, [r4, #30]	@ movhi
	.loc 3 190 0
	strh	r0, [r4, #32]	@ movhi
	.loc 3 191 0
	bne	.L16
	.loc 3 192 0
	ldrh	r3, [fp, #6]
	lsrs	r3, r3, #1
	strh	r3, [r4, #18]	@ movhi
	.loc 3 193 0
	movs	r3, #8
	strh	r3, [r4, #34]	@ movhi
.L16:
	.loc 3 197 0 discriminator 3
	ldr	r1, .L25+4
	.loc 3 183 0 discriminator 3
	movs	r3, #0
.LVL26:
.L17:
	.loc 3 197 0 discriminator 3
	strb	r3, [r3, r1]
	.loc 3 196 0 discriminator 3
	adds	r3, r3, #1
	cmp	r3, #32
	bne	.L17
	.loc 3 205 0
	ldrh	r5, [r4, #18]
	.loc 3 204 0
	smulbb	r6, r6, r0
	.loc 3 198 0
	movs	r3, #5
.LVL27:
	.loc 3 200 0
	cmp	r2, #1
	.loc 3 198 0
	strh	r3, [r4, #68]	@ movhi
	.loc 3 204 0
	uxth	r6, r6
	.loc 3 199 0
	mov	r3, #0
	.loc 3 205 0
	smulbb	r0, r0, r5
	.loc 3 202 0
	mov	r7, #640
	.loc 3 201 0
	it	eq
	strheq	r2, [r4, #68]	@ movhi
	.loc 3 199 0
	strh	r3, [r4, #70]	@ movhi
	.loc 3 205 0
	uxth	r0, r0
	.loc 3 202 0
	strh	r7, [r4, #72]	@ movhi
	.loc 3 204 0
	strh	r6, [r4, #8]	@ movhi
	.loc 3 205 0
	strh	r0, [r4, #74]	@ movhi
	.loc 3 206 0
	bl	Ftl_log2
.LVL28:
	.loc 3 207 0
	ldrh	r9, [fp, #12]
	.loc 3 210 0
	ldrh	r10, [r4, #34]
	.loc 3 206 0
	strh	r0, [r4, #76]	@ movhi
	.loc 3 207 0
	strh	r9, [r4, #78]	@ movhi
	.loc 3 209 0
	smulbb	r3, r6, r9
	.loc 3 210 0
	mov	r0, r10
	.loc 3 208 0
	strh	r9, [r4, #80]	@ movhi
	.loc 3 209 0
	strh	r3, [r4, #82]	@ movhi
	.loc 3 210 0
	bl	Ftl_log2
.LVL29:
	.loc 3 212 0
	lsl	r3, r10, #9
	.loc 3 210 0
	mov	r8, r0
	strh	r0, [r4, #84]	@ movhi
	.loc 3 218 0
	mul	r1, r9, r10
	.loc 3 212 0
	uxth	r3, r3
	.loc 3 218 0
	mov	r0, #5120
	.loc 3 221 0
	asr	r7, r7, r8
	.loc 3 212 0
	strh	r3, [r4, #86]	@ movhi
	.loc 3 213 0
	lsrs	r3, r3, #8
	.loc 3 223 0
	add	r8, r8, #9
	.loc 3 213 0
	strh	r3, [r4, #88]	@ movhi
	.loc 3 221 0
	adds	r7, r7, #2
	.loc 3 214 0
	ldrh	r3, [fp, #20]
	strh	r3, [r4, #90]	@ movhi
	.loc 3 216 0
	mul	r3, r5, r6
	.loc 3 223 0
	lsls	r5, r5, #6
	asr	r5, r5, r8
	.loc 3 216 0
	str	r3, [r4, #24]
	.loc 3 217 0
	mul	r3, r10, r3
	mul	r3, r9, r3
	asrs	r3, r3, #11
	str	r3, [r4, #92]
	.loc 3 218 0
	bl	__aeabi_idiv
.LVL30:
	uxth	r0, r0
	.loc 3 223 0
	strh	r5, [r4, #100]	@ movhi
	uxth	r5, r5
	.loc 3 226 0
	mov	r1, r6
	.loc 3 219 0
	cmp	r0, #4
	.loc 3 221 0
	strh	r7, [r4, #98]	@ movhi
	.loc 3 220 0
	itet	ls
	movls	r3, #4
	.loc 3 218 0
	strhhi	r0, [r4, #96]	@ movhi
	.loc 3 220 0
	strhls	r3, [r4, #96]	@ movhi
	.loc 3 224 0
	mul	r3, r6, r5
	.loc 3 226 0
	ldrh	r0, [r4, #96]
	adds	r5, r5, #8
	.loc 3 224 0
	str	r3, [r4, #104]
	.loc 3 226 0
	bl	__aeabi_uidiv
.LVL31:
	uxtah	r0, r5, r0
	.loc 3 227 0
	cmp	r6, #1
	.loc 3 228 0
	it	eq
	addeq	r0, r0, #4
	str	r0, [r4, #4]
	.loc 3 231 0
	ldrh	r0, [r4, #4]
	bl	FtlSysBlkNumInit
.LVL32:
	.loc 3 232 0
	ldr	r3, [r4, #4]
	.loc 3 236 0
	movs	r0, #0
	str	r0, [r4, #116]
	.loc 3 232 0
	str	r3, [r4, #108]
	.loc 3 234 0
	ldr	r3, [r4, #20]
	lsls	r2, r3, #2
	ldrh	r3, [r4, #78]
	muls	r3, r2, r3
	ldrh	r2, [r4, #84]
	adds	r2, r2, #9
	lsrs	r3, r3, r2
	adds	r3, r3, #2
	strh	r3, [r4, #112]	@ movhi
	.loc 3 235 0
	movs	r3, #32
	strh	r3, [r4, #114]	@ movhi
	.loc 3 237 0
	ldrh	r3, [r4, #96]
	adds	r3, r3, #3
	strh	r3, [r4, #96]	@ movhi
	.loc 3 238 0
	ldr	r3, [r4, #104]
	adds	r3, r3, #3
	str	r3, [r4, #104]
	.loc 3 263 0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL33:
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.word	.LANCHOR0+36
.LFE155:
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	1
	.global	IsBlkInVendorPart
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
.LFB158:
	.loc 3 514 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL34:
	.loc 3 517 0
	ldr	r2, .L34
	ldrh	r3, [r2, #120]
	cbz	r3, .L33
.LBB218:
.LBB219:
	.loc 3 519 0
	ldr	r3, [r2, #124]
	ldrh	r2, [r2, #96]
	add	r2, r3, r2, lsl #1
.L29:
	.loc 3 518 0
	cmp	r3, r2
	bne	.L30
.L33:
.LBE219:
.LBE218:
	.loc 3 524 0
	movs	r0, #0
.LVL35:
	bx	lr
.LVL36:
.L30:
.LBB221:
.LBB220:
	.loc 3 519 0
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L29
	.loc 3 520 0
	movs	r0, #1
.LVL37:
.LBE220:
.LBE221:
	.loc 3 525 0
	bx	lr
.L35:
	.align	2
.L34:
	.word	.LANCHOR0
.LFE158:
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	1
	.global	sftl_get_density
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
	.fnstart
.LFB171:
	.loc 3 1212 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 3 1214 0
	ldr	r3, .L37
	ldr	r0, [r3, #116]
	bx	lr
.L38:
	.align	2
.L37:
	.word	.LANCHOR0
.LFE171:
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.align	1
	.global	FtlBbmMapBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
.LFB172:
	.file 4 "drivers/rkflash/rksftl/sftl_bbm.c"
	.loc 4 5 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL38:
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
.LCFI2:
.LVL39:
	.loc 4 5 0
	mov	r5, r0
	.loc 4 10 0
	ldr	r4, .L40
	ldrh	r7, [r4, #74]
	mov	r1, r7
	bl	__aeabi_uidiv
.LVL40:
	uxth	r6, r0
.LVL41:
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
.LVL42:
	.loc 4 13 0
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	.loc 4 15 0
	ldr	r2, [r2, #156]
	lsrs	r1, r3, #5
	and	r7, r3, #31
	movs	r0, #1
	lsls	r0, r0, r7
	ldr	r7, [r2, r1, lsl #2]
	orrs	r0, r0, r7
	str	r0, [r2, r1, lsl #2]
	.loc 4 16 0
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L40+4
	bl	printk
.LVL43:
	.loc 4 17 0
	ldrh	r3, [r4, #134]
	.loc 4 19 0
	movs	r0, #0
	.loc 4 17 0
	adds	r3, r3, #1
	strh	r3, [r4, #134]	@ movhi
	.loc 4 19 0
	add	sp, sp, #12
.LCFI3:
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.LVL44:
.L41:
	.align	2
.L40:
	.word	.LANCHOR0
	.word	.LC0
.LFE172:
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	1
	.global	FtlBbmIsBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
.LFB173:
	.loc 4 24 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL45:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI4:
	.loc 4 24 0
	mov	r7, r0
	.loc 4 30 0
	ldr	r5, .L43
	ldrh	r6, [r5, #74]
	.loc 4 31 0
	mov	r1, r6
	bl	__aeabi_uidivmod
.LVL46:
	.loc 4 30 0
	mov	r0, r7
	.loc 4 31 0
	uxth	r4, r1
.LVL47:
	.loc 4 30 0
	mov	r1, r6
	bl	__aeabi_uidiv
.LVL48:
	uxth	r0, r0
.LVL49:
	.loc 4 34 0
	lsrs	r2, r4, #5
	.loc 4 33 0
	add	r5, r5, r0, lsl #2
	.loc 4 34 0
	and	r4, r4, #31
.LVL50:
	ldr	r3, [r5, #156]
	ldr	r0, [r3, r2, lsl #2]
.LVL51:
	lsrs	r0, r0, r4
	.loc 4 36 0
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.LVL52:
.L44:
	.align	2
.L43:
	.word	.LANCHOR0
.LFE173:
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	1
	.global	FtlBbtInfoPrint
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
	.fnstart
.LFB174:
	.loc 4 39 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
.LFE174:
	.fnend
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	1
	.global	FtlBbtMemInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
.LFB177:
	.loc 4 149 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 4 150 0
	ldr	r0, .L47
	movw	r3, #65535
.LBB227:
.LBB228:
.LBB229:
	.loc 1 202 0
	movs	r2, #16
	movs	r1, #255
.LBE229:
.LBE228:
.LBE227:
	.loc 4 150 0
	strh	r3, [r0, #128]	@ movhi
	.loc 4 151 0
	movs	r3, #0
	strh	r3, [r0, #134]	@ movhi
.LBB232:
.LBB231:
.LBB230:
	.loc 1 202 0
	adds	r0, r0, #140
	b	memset
.LVL53:
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
.LBE230:
.LBE231:
.LBE232:
.LFE177:
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	1
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
.LFB178:
	.loc 4 157 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	.loc 4 160 0
	ldr	r3, .L56
	.loc 4 157 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI5:
	.loc 4 160 0
	movs	r5, #0
	.loc 4 159 0
	mov	r4, r5
	.loc 4 160 0
	ldrh	r2, [r3, #74]
	ldrh	r6, [r3, #30]
	muls	r6, r2, r6
.L50:
	uxth	r0, r5
.LVL54:
	.loc 4 160 0 is_stmt 0 discriminator 1
	cmp	r0, r6
.LVL55:
	blt	.L52
	.loc 4 169 0 is_stmt 1
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L52:
	.loc 4 162 0
	bl	FtlBbmIsBadBlock
.LVL56:
	cbz	r0, .L51
	.loc 4 164 0
	adds	r4, r4, #1
	uxth	r4, r4
.L51:
	adds	r5, r5, #1
	b	.L50
.L57:
	.align	2
.L56:
	.word	.LANCHOR0
.LFE178:
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	1
	.global	V2P_block
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
.LFB182:
	.loc 2 5 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL57:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI6:
	.loc 2 5 0
	mov	r5, r1
	.loc 2 8 0
	ldr	r4, .L59
	.loc 2 5 0
	mov	r7, r0
	.loc 2 8 0
	ldrh	r6, [r4, #32]
	mov	r1, r6
.LVL58:
	bl	__aeabi_uidiv
.LVL59:
	ldrh	r4, [r4, #74]
	smulbb	r5, r6, r5
.LVL60:
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
.LVL61:
	adds	r0, r5, r1
	add	r0, r0, r4
	.loc 2 10 0
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL62:
.L60:
	.align	2
.L59:
	.word	.LANCHOR0
.LFE182:
	.fnend
	.size	V2P_block, .-V2P_block
	.align	1
	.global	P2V_plane
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
.LFB183:
	.loc 2 13 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL63:
	.loc 2 16 0
	ldr	r3, .L62
	.loc 2 13 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI7:
	.loc 2 13 0
	mov	r6, r0
	.loc 2 16 0
	ldrh	r5, [r3, #32]
	ldrh	r1, [r3, #74]
	bl	__aeabi_uidiv
.LVL64:
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
.LVL65:
	add	r1, r1, r4
	.loc 2 18 0
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.LVL66:
.L63:
	.align	2
.L62:
	.word	.LANCHOR0
.LFE183:
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	1
	.global	P2V_block_in_plane
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
.LFB184:
	.loc 2 21 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL67:
	push	{r4, lr}
	.save {r4, lr}
.LCFI8:
	.loc 2 24 0
	ldr	r4, .L65
	ldrh	r1, [r4, #74]
	bl	__aeabi_uidivmod
.LVL68:
	uxth	r0, r1
	.loc 2 25 0
	ldrh	r1, [r4, #32]
	bl	__aeabi_uidiv
.LVL69:
	.loc 2 27 0
	uxth	r0, r0
	pop	{r4, pc}
.L66:
	.align	2
.L65:
	.word	.LANCHOR0
.LFE184:
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	1
	.global	ftl_cmp_data_ver
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
.LFB185:
	.loc 2 31 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL70:
	.loc 2 34 0
	cmp	r0, r1
	bls	.L68
	.loc 2 36 0
	subs	r0, r0, r1
.LVL71:
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.LVL72:
.L68:
	.loc 2 40 0
	subs	r0, r1, r0
.LVL73:
	cmp	r0, #-2147483648
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	.loc 2 44 0
	bx	lr
.LFE185:
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	1
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
.LFB187:
	.loc 2 76 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL74:
	.loc 2 79 0
	ldr	r3, .L71
.LBB238:
.LBB239:
.LBB240:
	.loc 1 202 0
	mov	r1, #2048
.LBE240:
.LBE239:
.LBE238:
	.loc 2 76 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI9:
	.loc 2 79 0
	movs	r4, #0
	.loc 2 82 0
	strh	r0, [r3, #188]	@ movhi
.LBB245:
.LBB243:
.LBB241:
	.loc 1 202 0
	add	r0, r3, #196
.LVL75:
.LBE241:
.LBE243:
.LBE245:
	.loc 2 79 0
	strh	r4, [r3, #190]	@ movhi
	.loc 2 80 0
	strh	r4, [r3, #192]	@ movhi
	.loc 2 81 0
	strh	r4, [r3, #194]	@ movhi
.LBB246:
.LBB244:
.LBB242:
	.loc 1 202 0
	bl	__memzero
.LVL76:
.LBE242:
.LBE244:
.LBE246:
	.loc 2 85 0
	mov	r0, r4
	pop	{r4, pc}
.L72:
	.align	2
.L71:
	.word	.LANCHOR0
.LFE187:
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	1
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
.LFB188:
	.loc 2 88 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 2 89 0
	ldr	r3, .L74
	ldrh	r0, [r3, #194]
	.loc 2 90 0
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L75:
	.align	2
.L74:
	.word	.LANCHOR0
.LFE188:
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	1
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
.LFB189:
	.loc 2 93 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 2 94 0
	ldr	r3, .L77
	ldrh	r0, [r3, #194]
	.loc 2 95 0
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L78:
	.align	2
.L77:
	.word	.LANCHOR0
.LFE189:
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	1
	.global	FtlFreeSysBLkSort
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
.LFB191:
	.loc 2 116 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI10:
	.loc 2 123 0
	movs	r3, #0
	ldr	r6, .L88
	mov	r4, r6
.L80:
	.loc 2 123 0 is_stmt 0 discriminator 1
	ldrh	r1, [r6, #194]
	uxth	r2, r3
	adds	r7, r3, #1
	cmp	r1, r2
	bhi	.L81
	movs	r1, #0
.LVL77:
.L82:
	.loc 2 128 0 is_stmt 1 discriminator 1
	ldrh	ip, [r4, #194]
	add	r3, ip, #-1
.LVL78:
	cmp	r1, r3
	blt	.L87
	.loc 2 144 0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL79:
.L81:
	.loc 2 124 0 discriminator 3
	ldrh	r2, [r6, #190]
	uxth	r5, r3
	adds	r3, r2, r5
	add	r3, r6, r3, lsl #1
	ldrh	r0, [r3, #196]
	bl	P2V_block_in_plane
.LVL80:
	.loc 2 125 0 discriminator 3
	ldr	r2, [r6, #2248]
	ldr	r3, [r6, #2244]
	ldrh	r2, [r2, r0, lsl #1]
	str	r2, [r3, r5, lsl #2]
	mov	r3, r7
	b	.L80
.LVL81:
.L87:
	.loc 2 130 0
	adds	r5, r1, #1
	.loc 2 131 0
	ldr	r0, [r4, #2244]
.LVL82:
	.loc 2 130 0
	uxth	r5, r5
	mov	r3, r1
.LVL83:
	mov	r2, r5
.L83:
	.loc 2 130 0 is_stmt 0 discriminator 1
	cmp	r2, ip
	bcc	.L85
	.loc 2 134 0 is_stmt 1
	cmp	r1, r3
	beq	.L86
	.loc 2 136 0
	ldr	r2, [r0, r1, lsl #2]
	.loc 2 135 0
	ldr	r6, [r0, r3, lsl #2]
	.loc 2 136 0
	str	r2, [r0, r3, lsl #2]
	.loc 2 137 0
	ldr	r2, [r4, #2244]
	str	r6, [r2, r1, lsl #2]
	.loc 2 139 0
	ldrh	r2, [r4, #190]
	add	r3, r3, r2
.LVL84:
	.loc 2 140 0
	add	r2, r2, r1
	.loc 2 139 0
	adds	r3, r3, #4
	.loc 2 140 0
	adds	r2, r2, #4
	.loc 2 139 0
	add	r3, r4, r3, lsl #1
	.loc 2 140 0
	add	r2, r4, r2, lsl #1
	.loc 2 139 0
	ldrh	r0, [r3, #188]
.LVL85:
	.loc 2 140 0
	ldrh	r1, [r2, #188]
	strh	r1, [r3, #188]	@ movhi
	.loc 2 141 0
	strh	r0, [r2, #188]	@ movhi
.LVL86:
.L86:
	.loc 2 130 0 discriminator 1
	mov	r1, r5
	b	.L82
.LVL87:
.L85:
	.loc 2 131 0
	ldr	r6, [r0, r2, lsl #2]
.LVL88:
	ldr	r7, [r0, r3, lsl #2]
	cmp	r7, r6
	it	hi
	movhi	r3, r2
	.loc 2 130 0
	adds	r2, r2, #1
	uxth	r2, r2
	b	.L83
.L89:
	.align	2
.L88:
	.word	.LANCHOR0
.LFE191:
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	1
	.global	IsInFreeQueue
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
.LFB192:
	.loc 2 147 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL89:
.LBB247:
.LBB248:
	.loc 2 94 0
	ldr	r3, .L96
.LBE248:
.LBE247:
	.loc 2 147 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI11:
.LBB250:
.LBB249:
	.loc 2 94 0
	ldrh	r4, [r3, #194]
.LBE249:
.LBE250:
	.loc 2 152 0
	cmp	r4, #1024
	beq	.L94
	.loc 2 155 0
	ldrh	r5, [r3, #190]
	movs	r1, #0
.LVL90:
.L92:
	.loc 2 154 0 discriminator 1
	cmp	r1, r4
	bcc	.L93
.L94:
	.loc 2 148 0
	movs	r0, #0
.LVL91:
	pop	{r4, r5, pc}
.LVL92:
.L93:
	.loc 2 155 0
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #196]
	cmp	r2, r0
	beq	.L95
	.loc 2 154 0 discriminator 2
	adds	r1, r1, #1
	b	.L92
.L95:
	.loc 2 156 0
	movs	r0, #1
.LVL93:
	.loc 2 162 0
	pop	{r4, r5, pc}
.L97:
	.align	2
.L96:
	.word	.LANCHOR0
.LFE192:
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	1
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
.LFB193:
	.loc 2 165 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LBB251:
.LBB252:
	.loc 2 89 0
	ldr	r2, .L101
	ldrh	r1, [r2, #194]
.LBE252:
.LBE251:
	.loc 2 168 0
	cbz	r1, .L100
.LBB253:
	.loc 2 170 0
	ldrh	r3, [r2, #190]
	.loc 2 171 0
	subs	r1, r1, #1
	strh	r1, [r2, #194]	@ movhi
	.loc 2 170 0
	add	r0, r2, r3, lsl #1
	.loc 2 172 0
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 170 0
	ldrh	r0, [r0, #196]
	.loc 2 172 0
	strh	r3, [r2, #190]	@ movhi
	bx	lr
.L100:
.LBE253:
	.loc 2 166 0
	movw	r0, #65535
	.loc 2 176 0
	bx	lr
.L102:
	.align	2
.L101:
	.word	.LANCHOR0
.LFE193:
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	1
	.global	insert_data_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
.LFB196:
	.loc 2 217 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL94:
	.loc 2 225 0
	movw	r3, #65535
	.loc 2 217 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI12:
.LVL95:
	.loc 2 225 0
	cmp	r0, r3
	beq	.L104
	.loc 2 230 0
	ldr	r2, .L116
	movs	r5, #6
	muls	r5, r0, r5
	ldr	ip, [r2, #2252]
	mov	r1, r2
	add	r4, ip, r5
.LVL96:
	.loc 2 231 0
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [ip, r5]	@ movhi
	.loc 2 233 0
	ldr	r3, [r2, #2256]
.LVL97:
	cbnz	r3, .L105
	.loc 2 234 0
	str	r4, [r2, #2256]
.LVL98:
.L104:
	.loc 2 281 0
	movs	r0, #0
.LVL99:
	add	sp, sp, #12
.LCFI13:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL100:
.L105:
.LCFI14:
	.loc 2 239 0
	ldrh	r6, [r4, #4]
	.loc 2 238 0
	lsls	r7, r0, #1
	ldr	r8, [r2, #2260]
	ldrh	r2, [r8, r0, lsl #1]
.LVL101:
	.loc 2 240 0
	cmp	r6, #0
	beq	.L114
	.loc 2 239 0
	muls	r6, r2, r6
.LVL102:
.L106:
	.loc 2 244 0
	ldr	lr, [r1, #2252]
	sub	r2, r3, lr
.LVL103:
	asr	r9, r2, #1
	ldr	r2, .L116+4
	mul	r2, r2, r9
	.loc 2 252 0
	ldr	r9, [r1, #2248]
	add	r7, r9, r7
	.loc 2 244 0
	uxth	r2, r2
	.loc 2 252 0
	str	r7, [sp, #4]
.LVL104:
.L112:
	.loc 2 247 0
	ldrh	r7, [r3, #4]
	.loc 2 246 0
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	.loc 2 248 0
	cbz	r7, .L115
	.loc 2 247 0
	mul	r7, r7, fp
.LVL105:
.L107:
	.loc 2 251 0
	cmp	r6, r7
	bne	.L108
	.loc 2 252 0
	ldr	r7, [sp, #4]
.LVL106:
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L110
.LVL107:
.L109:
	.loc 2 270 0
	strh	r2, [ip, r5]	@ movhi
	.loc 2 271 0
	ldrh	r2, [r3, #2]
	strh	r2, [r4, #2]	@ movhi
	.loc 2 273 0
	ldr	r2, [r1, #2256]
	cmp	r3, r2
	.loc 2 277 0
	ittte	ne
	ldrhne	r4, [r3, #2]
.LVL108:
	movne	r2, #6
	ldrne	r1, [r1, #2252]
	.loc 2 274 0
	strheq	r0, [r3, #2]	@ movhi
.LVL109:
	.loc 2 275 0
	iteee	eq
	streq	r4, [r1, #2256]
	.loc 2 277 0
	mulne	r2, r2, r4
.LVL110:
	strhne	r0, [r1, r2]	@ movhi
	.loc 2 278 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L104
.LVL111:
.L114:
	.loc 2 241 0
	mov	r6, #-1
.LVL112:
	b	.L106
.LVL113:
.L115:
	.loc 2 249 0
	mov	r7, #-1
.LVL114:
	b	.L107
.L108:
	.loc 2 256 0
	bcc	.L109
.L110:
	.loc 2 259 0
	ldrh	r7, [r3]
.LVL115:
	movw	r10, #65535
	cmp	r7, r10
	bne	.L111
	.loc 2 260 0
	strh	r2, [r4, #2]	@ movhi
	.loc 2 261 0
	strh	r0, [r3]	@ movhi
	.loc 2 262 0
	str	r4, [r1, #2264]
	.loc 2 263 0
	b	.L104
.L111:
	.loc 2 267 0
	movs	r3, #6
.LVL116:
	.loc 2 246 0
	mov	r2, r7
	.loc 2 267 0
	mla	r3, r3, r7, lr
.LVL117:
	.loc 2 246 0
	b	.L112
.L117:
	.align	2
.L116:
	.word	.LANCHOR0
	.word	-1431655765
.LFE196:
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	1
	.global	INSERT_DATA_LIST
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
.LFB195:
	.loc 2 186 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL118:
	push	{r3, lr}
	.save {r3, lr}
.LCFI15:
	.loc 2 187 0
	bl	insert_data_list
.LVL119:
	.loc 2 188 0
	ldr	r2, .L120
	ldrh	r3, [r2, #2268]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2268]	@ movhi
	.loc 2 189 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	bcs	.L118
.LBB256:
.LBB257:
	movs	r2, #189
	ldr	r1, .L120+4
	ldr	r0, .L120+8
.LBE257:
.LBE256:
	.loc 2 190 0
	pop	{r3, lr}
.LCFI16:
.LBB259:
.LBB258:
	.loc 2 189 0
	b	printk
.LVL120:
.L118:
.LCFI17:
	pop	{r3, pc}
.L121:
	.align	2
.L120:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC1
.LBE258:
.LBE259:
.LFE195:
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	1
	.global	insert_free_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
.LFB197:
	.loc 2 284 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL121:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI18:
	.loc 2 290 0
	movw	r4, #65535
	cmp	r0, r4
	beq	.L123
	.loc 2 293 0
	ldr	r2, .L129
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2252]
	mov	r5, r2
	add	r6, ip, r7
.LVL122:
	.loc 2 294 0
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	.loc 2 296 0
	ldr	r3, [r2, #2272]
.LVL123:
	cbnz	r3, .L124
	.loc 2 297 0
	str	r6, [r2, #2272]
.LVL124:
.L123:
	.loc 2 330 0
	movs	r0, #0
.LVL125:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL126:
.L124:
	.loc 2 303 0
	ldr	lr, [r2, #2252]
	.loc 2 301 0
	ldr	r8, [r2, #2248]
	.loc 2 303 0
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L129+4
	.loc 2 301 0
	ldrh	r9, [r8, r0, lsl #1]
.LVL127:
	.loc 2 303 0
	mul	r2, r2, r10
	.loc 2 310 0
	mov	r10, r4
	.loc 2 303 0
	uxth	r2, r2
.L127:
	.loc 2 307 0
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L125
	.loc 2 310 0
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L126
	.loc 2 311 0
	strh	r2, [r6, #2]	@ movhi
	.loc 2 312 0
	strh	r0, [r3]	@ movhi
	.loc 2 313 0
	b	.L123
.L126:
	.loc 2 316 0
	mla	r3, r1, r4, lr
	.loc 2 306 0
	mov	r2, r4
	b	.L127
.L125:
	.loc 2 319 0
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	.loc 2 320 0
	strh	r2, [ip, r7]	@ movhi
	.loc 2 322 0
	ldr	r2, [r5, #2272]
	cmp	r3, r2
	.loc 2 326 0
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r5, #2252]
	.loc 2 323 0
	strheq	r0, [r3, #2]	@ movhi
	.loc 2 324 0
	iteee	eq
	streq	r6, [r5, #2272]
	.loc 2 326 0
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	.loc 2 327 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L123
.L130:
	.align	2
.L129:
	.word	.LANCHOR0
	.word	-1431655765
.LFE197:
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	1
	.global	INSERT_FREE_LIST
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
.LFB194:
	.loc 2 179 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL128:
	push	{r3, lr}
	.save {r3, lr}
.LCFI19:
	.loc 2 180 0
	bl	insert_free_list
.LVL129:
	.loc 2 181 0
	ldr	r2, .L133
	ldrh	r3, [r2, #2276]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2276]	@ movhi
	.loc 2 182 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	bcs	.L131
.LBB262:
.LBB263:
	movs	r2, #182
	ldr	r1, .L133+4
	ldr	r0, .L133+8
.LBE263:
.LBE262:
	.loc 2 183 0
	pop	{r3, lr}
.LCFI20:
.LBB265:
.LBB264:
	.loc 2 182 0
	b	printk
.LVL130:
.L131:
.LCFI21:
	pop	{r3, pc}
.L134:
	.align	2
.L133:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC1
.LBE264:
.LBE265:
.LFE194:
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	1
	.global	List_remove_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
.LFB198:
	.loc 2 333 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL131:
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI22:
	.loc 2 336 0
	movs	r5, #6
	ldr	r7, .L141
	muls	r5, r1, r5
	.loc 2 337 0
	movw	r3, #65535
	.loc 2 333 0
	mov	r8, r0
	.loc 2 336 0
	ldr	r6, [r7, #2252]
	adds	r4, r6, r5
.LVL132:
	.loc 2 337 0
	ldrh	r2, [r4, #2]
	cmp	r2, r3
	bne	.L136
	.loc 2 337 0 is_stmt 0 discriminator 1
	ldr	r3, [r0]
	cmp	r4, r3
	beq	.L136
	.loc 2 337 0 discriminator 2
	movw	r2, #337
	ldr	r1, .L141+4
.LVL133:
	ldr	r0, .L141+8
	bl	printk
.LVL134:
.L136:
	.loc 2 339 0 is_stmt 1
	ldr	r3, [r8]
	movw	r2, #65535
	cmp	r4, r3
	ldrh	r3, [r6, r5]
	bne	.L137
	.loc 2 340 0
	cmp	r3, r2
	.loc 2 343 0
	ittee	ne
	ldrne	r1, [r7, #2252]
.LVL135:
	movne	r0, #6
	.loc 2 341 0
	moveq	r3, #0
	streq	r3, [r8]
	.loc 2 343 0
	ittt	ne
	mlane	r3, r0, r3, r1
	strne	r3, [r8]
	.loc 2 344 0
	strhne	r2, [r3, #2]	@ movhi
.L139:
	.loc 2 355 0
	movw	r3, #65535
	.loc 2 357 0
	movs	r0, #0
	.loc 2 355 0
	strh	r3, [r6, r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	.loc 2 357 0
	pop	{r4, r5, r6, r7, r8, pc}
.LVL136:
.L137:
	.loc 2 346 0
	cmp	r3, r2
	ldrh	r1, [r4, #2]
.LVL137:
	mov	r2, #6
	.loc 2 348 0
	ldr	r0, [r7, #2252]
	.loc 2 346 0
	bne	.L140
	.loc 2 348 0
	muls	r2, r1, r2
	strh	r3, [r0, r2]	@ movhi
	b	.L139
.L140:
	.loc 2 351 0
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	.loc 2 352 0
	ldrh	r0, [r4, #2]
	.loc 2 353 0
	ldrh	r1, [r6, r5]
	ldr	r3, [r7, #2252]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L139
.L142:
	.align	2
.L141:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC1
.LFE198:
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	1
	.global	List_pop_index_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
.LFB199:
	.loc 2 360 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL138:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI23:
	.loc 2 364 0
	ldr	r3, [r0]
.LVL139:
	cbz	r3, .L149
	.loc 2 369 0
	ldr	r2, .L150
	.loc 2 368 0
	movw	r5, #65535
	.loc 2 369 0
	movs	r6, #6
	ldr	r2, [r2, #2252]
.L145:
	.loc 2 368 0
	cbnz	r1, .L146
.L148:
	.loc 2 372 0
	ldr	r4, .L150+4
	subs	r3, r3, r2
.LVL140:
	asrs	r3, r3, #1
	muls	r4, r3, r4
	.loc 2 373 0
	uxth	r1, r4
.LVL141:
	bl	List_remove_node
.LVL142:
	uxth	r0, r4
	.loc 2 375 0
	pop	{r4, r5, r6, pc}
.LVL143:
.L146:
	.loc 2 368 0 discriminator 1
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L148
	.loc 2 370 0
	subs	r1, r1, #1
.LVL144:
	.loc 2 369 0
	mla	r3, r6, r4, r2
	.loc 2 370 0
	uxth	r1, r1
.LVL145:
	b	.L145
.L149:
	.loc 2 365 0
	movw	r0, #65535
.LVL146:
	.loc 2 376 0
	pop	{r4, r5, r6, pc}
.L151:
	.align	2
.L150:
	.word	.LANCHOR0
	.word	-1431655765
.LFE199:
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	1
	.global	List_pop_head_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_pop_head_node, %function
List_pop_head_node:
	.fnstart
.LFB200:
	.loc 2 379 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL147:
	.loc 2 380 0
	movs	r1, #0
	b	List_pop_index_node
.LVL148:
.LFE200:
	.fnend
	.size	List_pop_head_node, .-List_pop_head_node
	.align	1
	.global	List_get_gc_head_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
.LFB201:
	.loc 2 384 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL149:
	.loc 2 388 0
	ldr	r2, .L159
	.loc 2 384 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI24:
	.loc 2 388 0
	ldr	r3, [r2, #2256]
.LVL150:
	cbz	r3, .L158
	.loc 2 393 0
	ldr	r1, [r2, #2252]
	movs	r4, #6
	.loc 2 392 0
	movw	r2, #65535
.L155:
	cbz	r0, .L156
	.loc 2 392 0 is_stmt 0 discriminator 1
	ldrh	r3, [r3]
.LVL151:
	cmp	r3, r2
	bne	.L157
.LVL152:
.L158:
	.loc 2 389 0 is_stmt 1
	movw	r0, #65535
.LVL153:
	pop	{r4, pc}
.LVL154:
.L157:
	.loc 2 394 0
	subs	r0, r0, #1
.LVL155:
	.loc 2 393 0
	mla	r3, r4, r3, r1
.LVL156:
	.loc 2 394 0
	uxth	r0, r0
.LVL157:
	b	.L155
.L156:
	.loc 2 398 0
	ldr	r0, .L159+4
.LVL158:
	subs	r3, r3, r1
.LVL159:
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	.loc 2 401 0
	pop	{r4, pc}
.L160:
	.align	2
.L159:
	.word	.LANCHOR0
	.word	-1431655765
.LFE201:
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	1
	.global	List_update_data_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
.LFB202:
	.loc 2 404 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL160:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI25:
	.loc 2 404 0
	mov	r5, r0
	.loc 2 413 0
	ldr	r4, .L169
	ldrh	r3, [r4, #2280]
	cmp	r3, r0
	beq	.L163
	.loc 2 413 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2328]
	cmp	r3, r0
	beq	.L163
	.loc 2 413 0 discriminator 2
	ldrh	r3, [r4, #2376]
	cmp	r3, r0
	beq	.L163
.LBB268:
.LBB269:
	.loc 2 416 0 is_stmt 1
	movs	r3, #6
	ldr	r1, [r4, #2252]
	muls	r3, r0, r3
	.loc 2 418 0
	ldr	r2, [r4, #2256]
	.loc 2 416 0
	adds	r7, r1, r3
.LVL161:
	.loc 2 418 0
	cmp	r7, r2
	beq	.L163
	.loc 2 421 0
	ldr	r2, [r4, #2260]
	.loc 2 422 0
	ldrh	r6, [r7, #4]
	ldrh	r2, [r2, r0, lsl #1]
	.loc 2 424 0
	muls	r6, r2, r6
.LVL162:
	.loc 2 426 0
	ldrh	r2, [r7, #2]
	movw	r0, #65535
	.loc 2 424 0
	it	eq
	moveq	r6, #-1
	.loc 2 426 0
	cmp	r2, r0
	bne	.L166
	ldrh	r3, [r1, r3]
	cmp	r3, r2
	bne	.L166
	mov	r2, #426
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	printk
.LVL163:
.L166:
	.loc 2 427 0
	ldrh	r3, [r7, #2]
	movs	r2, #6
	muls	r2, r3, r2
	.loc 2 428 0
	ldr	r3, .L169+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	.loc 2 429 0
	ldr	r1, [r4, #2260]
	.loc 2 430 0
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2252]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	.loc 2 432 0
	muls	r3, r0, r3
.LVL164:
	it	eq
	moveq	r3, #-1
	.loc 2 434 0
	cmp	r6, r3
	bcs	.L163
	.loc 2 435 0
	mov	r1, r5
	ldr	r0, .L169+16
	bl	List_remove_node
.LVL165:
	ldrh	r3, [r4, #2268]
	cbnz	r3, .L168
	movw	r2, #435
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	printk
.LVL166:
.L168:
	ldrh	r3, [r4, #2268]
	.loc 2 436 0
	mov	r0, r5
	.loc 2 435 0
	subs	r3, r3, #1
	strh	r3, [r4, #2268]	@ movhi
	.loc 2 436 0
	bl	INSERT_DATA_LIST
.LVL167:
.L163:
.LBE269:
.LBE268:
	.loc 2 439 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL168:
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC1
	.word	-1431655765
	.word	.LANCHOR0+2256
.LFE202:
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	1
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
.LFB205:
	.loc 2 516 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL169:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI26:
	.loc 2 516 0
	mov	r4, r0
	.loc 2 520 0
	ldrh	r1, [r0, #10]
	movs	r3, #0
	ldr	r2, [r0, #12]
.LVL170:
.L172:
	uxth	r5, r3
	.loc 2 520 0 is_stmt 0 discriminator 1
	cmp	r5, r1
	bcc	.L174
.L175:
.LBB272:
.LBB273:
	.loc 2 532 0 is_stmt 1
	mov	r2, #532
	ldr	r1, .L181
	ldr	r0, .L181+4
	bl	printk
.LVL171:
	b	.L176
.L174:
.LBE273:
.LBE272:
	.loc 2 521 0
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L172
	.loc 2 522 0
	bl	FtlFreeSysBlkQueueOut
.LVL172:
	strh	r0, [r7]	@ movhi
	.loc 2 523 0
	cbz	r0, .L173
	.loc 2 526 0
	ldr	r3, [r4, #28]
	.loc 2 524 0
	strh	r6, [r4, #2]	@ movhi
	.loc 2 525 0
	strh	r5, [r4]	@ movhi
	.loc 2 526 0
	adds	r3, r3, #1
	str	r3, [r4, #28]
	.loc 2 527 0
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L173:
	.loc 2 532 0
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bls	.L175
.L176:
	.loc 2 534 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL173:
.L182:
	.align	2
.L181:
	.word	.LANCHOR1+73
	.word	.LC1
.LFE205:
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	1
	.global	select_l2p_ram_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
.LFB210:
	.loc 2 700 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI27:
	.loc 2 705 0
	movs	r1, #0
	ldr	r5, .L193
	.loc 2 706 0
	movs	r0, #12
	movw	r6, #65535
	.loc 2 705 0
	ldrh	r2, [r5, #114]
	.loc 2 706 0
	ldr	r3, [r5, #2424]
.L184:
	uxth	r4, r1
	.loc 2 705 0 discriminator 1
	cmp	r4, r2
	bcc	.L186
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	.loc 2 716 0
	mov	ip, #12
.L187:
	uxth	r6, r1
	.loc 2 715 0 discriminator 1
	cmp	r6, r2
	bcc	.L189
	.loc 2 724 0
	cmp	r4, r2
	bcc	.L185
	.loc 2 730 0
	ldrh	r7, [r5, #2428]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L190:
	uxth	r5, r1
	.loc 2 729 0 discriminator 1
	cmp	r5, r2
	bcc	.L192
	.loc 2 735 0
	cmp	r4, r2
	bcc	.L185
	.loc 2 735 0 is_stmt 0 discriminator 1
	movw	r2, #735
	ldr	r1, .L193+4
	ldr	r0, .L193+8
	bl	printk
.LVL174:
	b	.L185
.L186:
	adds	r1, r1, #1
	.loc 2 706 0 is_stmt 1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L184
.L185:
	.loc 2 737 0
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L189:
	.loc 2 716 0
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L188
	.loc 2 717 0
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L188:
	adds	r1, r1, #1
	b	.L187
.L192:
	.loc 2 730 0
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L191
	.loc 2 730 0 is_stmt 0 discriminator 1
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L191:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L190
.L194:
	.align	2
.L193:
	.word	.LANCHOR0
	.word	.LANCHOR1+99
	.word	.LC1
.LFE210:
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	1
	.global	FtlUpdateVaildLpn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
.LFB216:
	.loc 2 882 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL175:
	.loc 2 885 0
	ldr	r1, .L201
	.loc 2 882 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI28:
	mov	r3, r1
	.loc 2 885 0
	ldrh	r2, [r1, #2430]
	cmp	r2, #4
	bhi	.L196
	.loc 2 885 0 is_stmt 0 discriminator 1
	cbnz	r0, .L196
	.loc 2 885 0
	adds	r2, r2, #1
	strh	r2, [r1, #2430]	@ movhi
	.loc 2 894 0 is_stmt 1
	pop	{r4, r5, pc}
.L196:
	.loc 2 886 0
	movs	r2, #0
	ldrh	r1, [r3, #16]
	strh	r2, [r3, #2430]	@ movhi
.LBB276:
.LBB277:
	.loc 2 890 0
	movw	r5, #65535
.LBE277:
.LBE276:
	.loc 2 887 0
	str	r2, [r3, #2432]
.LBB279:
.LBB278:
	.loc 2 890 0
	ldr	r2, [r3, #2260]
	add	r1, r2, r1, lsl #1
.L197:
	.loc 2 888 0
	cmp	r2, r1
	bne	.L199
	pop	{r4, r5, pc}
.L199:
	.loc 2 890 0
	ldrh	r4, [r2], #2
	cmp	r4, r5
	.loc 2 891 0
	ittt	ne
	ldrne	r0, [r3, #2432]
.LVL176:
	addne	r0, r0, r4
	strne	r0, [r3, #2432]
	b	.L197
.L202:
	.align	2
.L201:
	.word	.LANCHOR0
.LBE278:
.LBE279:
.LFE216:
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	1
	.global	ftl_set_blk_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
.LFB222:
	.loc 2 1347 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL177:
	.loc 2 1347 0
	mov	r3, r0
	.loc 2 1348 0
	cbz	r1, .L204
	b	ftl_set_blk_mode.part.6
.LVL178:
.L204:
	.loc 2 1351 0
	ldr	r2, .L205
	lsrs	r0, r0, #5
	and	r3, r3, #31
.LVL179:
	ldr	r1, [r2]
.LVL180:
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L206:
	.align	2
.L205:
	.word	.LANCHOR0
.LFE222:
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	1
	.global	ftl_get_blk_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
.LFB223:
	.loc 2 1355 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL181:
	.loc 2 1356 0
	ldr	r3, .L208
	lsrs	r2, r0, #5
	and	r0, r0, #31
.LVL182:
	ldr	r3, [r3]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	.loc 2 1357 0
	and	r0, r0, #1
	bx	lr
.L209:
	.align	2
.L208:
	.word	.LANCHOR0
.LFE223:
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	1
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
.LFB229:
	.loc 2 1608 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL183:
	.loc 2 1610 0
	movs	r3, #0
	.loc 2 1608 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
.LCFI29:
	.loc 2 1610 0
	strh	r3, [r0, #4]	@ movhi
	add	r4, r0, r2, lsl #1
	.loc 2 1613 0
	movw	r7, #65535
	.loc 2 1611 0
	ldr	r3, .L217
	adds	r4, r4, #14
	ldrh	r5, [r3, #8]
.L211:
	.loc 2 1611 0 is_stmt 0 discriminator 1
	cmp	r2, r5
	bcc	.L213
	ldrh	r3, [r3, #78]
	add	r4, r0, #16
	.loc 2 1620 0 is_stmt 1
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
.LVL184:
	.loc 2 1622 0
	movs	r3, #0
	uxth	r1, r1
.L214:
	.loc 2 1618 0 discriminator 1
	uxth	r2, r3
.LVL185:
	cmp	r5, r2
	bhi	.L216
	.loc 2 1625 0
	pop	{r4, r5, r6, r7, pc}
.LVL186:
.L213:
	.loc 2 1613 0
	ldrh	r6, [r4, #2]!
	.loc 2 1611 0
	adds	r2, r2, #1
.LVL187:
	uxth	r2, r2
.LVL188:
	.loc 2 1613 0
	cmp	r6, r7
	.loc 2 1615 0
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L211
.LVL189:
.L216:
	.loc 2 1620 0
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	.loc 2 1622 0
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L214
.L218:
	.align	2
.L217:
	.word	.LANCHOR0
.LFE229:
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	1
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
.LFB232:
	.loc 2 2013 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL190:
	.loc 2 2016 0
	ldrh	r3, [r0, #4]
	.loc 2 2013 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI30:
	.loc 2 2016 0
	cbz	r3, .L219
	.loc 2 2018 0
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L219
.LBB282:
.LBB283:
	.loc 2 2020 0
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	.loc 2 2025 0
	movs	r5, #0
	.loc 2 2020 0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
.LVL191:
	.loc 2 2023 0
	ldr	r2, .L225
	ldrh	r4, [r2, #8]
	.loc 2 2021 0
	mov	r2, r3
.L222:
	cmp	r1, r2
	beq	.L224
.L219:
	pop	{r4, r5, pc}
.L224:
	.loc 2 2022 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2023 0
	cmp	r3, r4
	.loc 2 2022 0
	strb	r3, [r0, #6]
	.loc 2 2024 0
	itttt	eq
	ldrheq	r3, [r0, #2]
	.loc 2 2025 0
	strbeq	r5, [r0, #6]
	.loc 2 2024 0
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	.loc 2 2027 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r1, [r0, r3, lsl #1]
	b	.L222
.L226:
	.align	2
.L225:
	.word	.LANCHOR0
.LBE283:
.LBE282:
.LFE232:
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	1
	.global	make_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
.LFB234:
	.loc 2 2045 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL192:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI31:
	.loc 2 2045 0
	mov	r4, r0
	.loc 2 2048 0
	ldr	r6, .L232
	ldrh	r2, [r0]
	ldrh	r3, [r6, #16]
	cmp	r2, r3
	bcc	.L228
	.loc 2 2048 0 is_stmt 0 discriminator 1
	mov	r2, #2048
	ldr	r1, .L232+4
	ldr	r0, .L232+8
	bl	printk
.LVL193:
.L228:
	.loc 2 2051 0 is_stmt 1
	ldrh	r9, [r6, #8]
	add	r7, r4, #16
	.loc 2 2052 0
	ldr	r10, .L232+12
	.loc 2 2053 0
	movw	r8, #65535
	.loc 2 2049 0
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	.loc 2 2050 0
	strb	r5, [r4, #7]
.L229:
	.loc 2 2051 0 discriminator 1
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L231
	.loc 2 2061 0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	.loc 2 2064 0
	movs	r0, #0
	.loc 2 2061 0
	ldrh	r2, [r6, #78]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2062 0
	movs	r3, #1
	strb	r3, [r4, #9]
	.loc 2 2064 0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL194:
.L231:
	.loc 2 2052 0
	ldrh	r1, [r4]
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL195:
	.loc 2 2053 0
	strh	r8, [r7]	@ movhi
	.loc 2 2052 0
	mov	fp, r0
	.loc 2 2054 0
	bl	FtlBbmIsBadBlock
.LVL196:
	cbnz	r0, .L230
	.loc 2 2055 0
	strh	fp, [r7]	@ movhi
	.loc 2 2056 0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L230:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L229
.L233:
	.align	2
.L232:
	.word	.LANCHOR0
	.word	.LANCHOR1+121
	.word	.LC1
	.word	.LANCHOR0+36
.LFE234:
	.fnend
	.size	make_superblock, .-make_superblock
	.align	1
	.global	update_multiplier_value
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
.LFB243:
	.loc 2 2401 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL197:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI32:
	.loc 2 2405 0
	movs	r5, #0
	ldr	r6, .L240
	.loc 2 2401 0
	mov	r7, r0
	.loc 2 2403 0
	mov	r4, r5
	.loc 2 2405 0
	ldrh	r8, [r6, #8]
	.loc 2 2406 0
	add	r10, r6, #36
	.loc 2 2408 0
	ldrh	r9, [r6, #78]
.L235:
	.loc 2 2405 0 discriminator 1
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L237
	.loc 2 2410 0
	cbz	r4, .L239
	.loc 2 2411 0
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.LVL198:
.L238:
	.loc 2 2412 0
	ldr	r1, [r6, #2252]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	.loc 2 2414 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL199:
.L237:
	.loc 2 2406 0
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL200:
	.loc 2 2407 0
	bl	FtlBbmIsBadBlock
.LVL201:
	cbnz	r0, .L236
	.loc 2 2408 0
	add	r4, r4, r9
	uxth	r4, r4
.L236:
	adds	r5, r5, #1
	b	.L235
.L239:
	mov	r0, r4
	b	.L238
.L241:
	.align	2
.L240:
	.word	.LANCHOR0
.LFE243:
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	1
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
.LFB244:
	.loc 2 2417 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 2 2420 0
	ldr	r2, .L245
	ldr	r0, [r2, #2272]
	cbz	r0, .L243
	.loc 2 2421 0
	ldr	r3, [r2, #2252]
	subs	r0, r0, r3
	ldr	r3, .L245+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2248]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L243:
	.loc 2 2423 0
	bx	lr
.L246:
	.align	2
.L245:
	.word	.LANCHOR0
	.word	-1431655765
.LFE244:
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	1
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
.LFB245:
	.loc 2 2426 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL202:
	.loc 2 2431 0
	ldr	r1, .L255
	.loc 2 2426 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
.LCFI33:
	.loc 2 2431 0
	ldr	r3, [r1, #2272]
	cbz	r3, .L253
	.loc 2 2432 0
	ldrh	r2, [r1, #2276]
	.loc 2 2436 0
	movs	r6, #6
	.loc 2 2434 0
	ldr	r4, [r1, #2252]
	.loc 2 2436 0
	movw	r7, #65535
	.loc 2 2432 0
	rsb	r2, r2, r2, lsl #3
	.loc 2 2434 0
	subs	r3, r3, r4
	asrs	r3, r3, #1
	.loc 2 2432 0
	asrs	r2, r2, #3
	cmp	r0, r2
	.loc 2 2433 0
	it	gt
	uxthgt	r0, r2
	.loc 2 2434 0
	ldr	r2, .L255+4
	muls	r3, r2, r3
	.loc 2 2435 0
	movs	r2, #0
	.loc 2 2434 0
	uxth	r3, r3
.L250:
	.loc 2 2435 0 discriminator 1
	uxth	r5, r2
	cmp	r0, r5
	bls	.L252
	.loc 2 2436 0
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L254
.L252:
	.loc 2 2440 0
	ldr	r2, [r1, #2248]
	ldrh	r0, [r2, r3, lsl #1]
.LVL203:
	pop	{r4, r5, r6, r7, pc}
.LVL204:
.L254:
	mov	r3, r5
	b	.L250
.L253:
	.loc 2 2427 0
	mov	r0, r3
.LVL205:
	.loc 2 2443 0
	pop	{r4, r5, r6, r7, pc}
.L256:
	.align	2
.L255:
	.word	.LANCHOR0
	.word	-1431655765
.LFE245:
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	1
	.global	FtlPrintInfo2buf
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
.LFB150:
	.loc 3 11 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL206:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI34:
	mov	r7, r0
	.loc 3 17 0
	ldr	r6, .L269
	.loc 3 11 0
	.pad #20
	sub	sp, sp, #20
.LCFI35:
	.loc 3 15 0
	add	r5, r7, #12
	ldr	r1, .L269+4
	bl	strcpy
.LVL207:
	.loc 3 17 0
	ldr	r2, [r6, #92]
	mov	r0, r5
	ldr	r1, .L269+8
	bl	sprintf
.LVL208:
	add	r5, r5, r0
	.loc 3 18 0
	ldr	r1, .L269+12
	mov	r0, r5
	adds	r5, r5, #10
	bl	strcpy
.LVL209:
	.loc 3 19 0
	ldr	r2, [r6, #2436]
	mov	r0, r5
	ldr	r1, .L269+16
	bl	sprintf
.LVL210:
	add	r5, r5, r0
	.loc 3 20 0
	ldr	r2, [r6, #2432]
	ldr	r1, .L269+20
	mov	r0, r5
	bl	sprintf
.LVL211:
	add	r5, r5, r0
	.loc 3 21 0
	ldr	r2, [r6, #2440]
	ldr	r1, .L269+24
	mov	r0, r5
	bl	sprintf
.LVL212:
	add	r5, r5, r0
	.loc 3 22 0
	ldr	r2, [r6, #2444]
	ldr	r1, .L269+28
	mov	r0, r5
	bl	sprintf
.LVL213:
	add	r5, r5, r0
	.loc 3 23 0
	ldr	r2, [r6, #2448]
	ldr	r1, .L269+32
	mov	r0, r5
	bl	sprintf
.LVL214:
	add	r5, r5, r0
	.loc 3 24 0
	ldr	r2, [r6, #2452]
	ldr	r1, .L269+36
	mov	r0, r5
	bl	sprintf
.LVL215:
	add	r5, r5, r0
	.loc 3 25 0
	ldr	r2, [r6, #2456]
	ldr	r1, .L269+40
	mov	r0, r5
	bl	sprintf
.LVL216:
	add	r5, r5, r0
	.loc 3 26 0
	ldr	r2, [r6, #2460]
	ldr	r1, .L269+44
	mov	r0, r5
	bl	sprintf
.LVL217:
	.loc 3 27 0
	ldr	r2, [r6, #2464]
	.loc 3 26 0
	add	r5, r5, r0
	.loc 3 27 0
	ldr	r1, .L269+48
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
.LVL218:
	.loc 3 28 0
	ldr	r2, [r6, #2468]
	.loc 3 27 0
	add	r5, r5, r0
	.loc 3 28 0
	ldr	r1, .L269+52
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
.LVL219:
	add	r5, r5, r0
	.loc 3 29 0
	ldr	r2, [r6, #2472]
	ldr	r1, .L269+56
	mov	r0, r5
	bl	sprintf
.LVL220:
	add	r5, r5, r0
	.loc 3 30 0
	ldr	r2, [r6, #2476]
	ldr	r1, .L269+60
	mov	r0, r5
	bl	sprintf
.LVL221:
	add	r5, r5, r0
	.loc 3 31 0
	ldrh	r2, [r6, #134]
	ldr	r1, .L269+64
	mov	r0, r5
	bl	sprintf
.LVL222:
	add	r5, r5, r0
	.loc 3 32 0
	ldrh	r2, [r6, #2276]
	ldr	r1, .L269+68
	mov	r0, r5
	bl	sprintf
.LVL223:
	add	r5, r5, r0
	.loc 3 33 0
	ldr	r2, [r6, #2480]
	ldr	r1, .L269+72
	mov	r0, r5
	bl	sprintf
.LVL224:
	add	r5, r5, r0
	.loc 3 34 0
	ldr	r2, [r6, #2484]
	ldr	r1, .L269+76
	mov	r0, r5
	bl	sprintf
.LVL225:
	add	r5, r5, r0
	.loc 3 35 0
	ldr	r2, [r6, #2488]
	ldr	r1, .L269+80
	mov	r0, r5
	bl	sprintf
.LVL226:
	add	r5, r5, r0
	.loc 3 36 0
	ldr	r2, [r6, #2492]
	ldr	r1, .L269+84
	mov	r0, r5
	bl	sprintf
.LVL227:
	add	r5, r5, r0
	.loc 3 37 0
	ldr	r2, [r6, #2496]
	ldr	r1, .L269+88
	mov	r0, r5
	bl	sprintf
.LVL228:
	add	r5, r5, r0
	.loc 3 38 0
	ldr	r2, [r6, #2500]
	ldr	r1, .L269+92
	mov	r0, r5
	bl	sprintf
.LVL229:
	add	r5, r5, r0
	.loc 3 39 0
	ldrh	r2, [r6, #2534]
	ldr	r1, .L269+96
	mov	r0, r5
	bl	sprintf
.LVL230:
	add	r5, r5, r0
	.loc 3 40 0
	ldrh	r2, [r6, #2532]
	ldr	r1, .L269+100
	mov	r0, r5
	bl	sprintf
.LVL231:
	add	r5, r5, r0
	.loc 3 41 0
	ldr	r2, [r6, #116]
	ldr	r1, .L269+104
	mov	r0, r5
	bl	sprintf
.LVL232:
	add	r5, r5, r0
	.loc 3 42 0
	ldr	r2, [r6, #108]
	ldr	r1, .L269+108
	mov	r0, r5
	bl	sprintf
.LVL233:
	add	r5, r5, r0
	.loc 3 43 0
	ldr	r2, [r6, #4]
	ldr	r1, .L269+112
	mov	r0, r5
	bl	sprintf
.LVL234:
	add	r5, r5, r0
	.loc 3 44 0
	ldrh	r2, [r6, #194]
	ldr	r1, .L269+116
	mov	r0, r5
	bl	sprintf
.LVL235:
	add	r5, r5, r0
	.loc 3 45 0
	ldrh	r2, [r6, #16]
	ldr	r1, .L269+120
	mov	r0, r5
	bl	sprintf
.LVL236:
	add	r5, r5, r0
	.loc 3 46 0
	ldrh	r2, [r6, #2552]
	ldr	r1, .L269+124
	mov	r0, r5
	bl	sprintf
.LVL237:
	add	r5, r5, r0
	.loc 3 47 0
	ldr	r2, [r6, #20]
	ldr	r1, .L269+128
	mov	r0, r5
	bl	sprintf
.LVL238:
	add	r5, r5, r0
	.loc 3 48 0
	ldrh	r2, [r6, #2556]
	ldr	r1, .L269+132
	mov	r0, r5
	bl	sprintf
.LVL239:
	add	r5, r5, r0
	.loc 3 49 0
	ldrh	r2, [r6, #128]
	ldr	r1, .L269+136
	mov	r0, r5
	bl	sprintf
.LVL240:
	add	r5, r5, r0
	.loc 3 50 0
	ldrh	r2, [r6, #2282]
	ldr	r1, .L269+140
	mov	r0, r5
	bl	sprintf
.LVL241:
	add	r5, r5, r0
	.loc 3 51 0
	ldrb	r2, [r6, #2286]	@ zero_extendqisi2
	ldr	r1, .L269+144
	mov	r0, r5
	bl	sprintf
.LVL242:
	add	r5, r5, r0
	.loc 3 52 0
	ldrh	r2, [r6, #2280]
	ldr	r1, .L269+148
	mov	r0, r5
	bl	sprintf
.LVL243:
	add	r5, r5, r0
	.loc 3 53 0
	ldrb	r2, [r6, #2288]	@ zero_extendqisi2
	ldr	r1, .L269+152
	mov	r0, r5
	bl	sprintf
.LVL244:
	add	r5, r5, r0
	.loc 3 54 0
	ldrh	r2, [r6, #2284]
	ldr	r1, .L269+156
	mov	r0, r5
	bl	sprintf
.LVL245:
	.loc 3 55 0
	ldr	r3, [r6, #2260]
	.loc 3 54 0
	add	r5, r5, r0
	.loc 3 55 0
	ldrh	r2, [r6, #2280]
	mov	r0, r5
	ldr	r1, .L269+160
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
.LVL246:
	add	r5, r5, r0
	.loc 3 56 0
	ldrh	r2, [r6, #2330]
	ldr	r1, .L269+164
	mov	r0, r5
	bl	sprintf
.LVL247:
	add	r5, r5, r0
	.loc 3 57 0
	ldrb	r2, [r6, #2334]	@ zero_extendqisi2
	ldr	r1, .L269+168
	mov	r0, r5
	bl	sprintf
.LVL248:
	add	r5, r5, r0
	.loc 3 58 0
	ldrh	r2, [r6, #2328]
	ldr	r1, .L269+172
	mov	r0, r5
	bl	sprintf
.LVL249:
	add	r5, r5, r0
	.loc 3 59 0
	ldrb	r2, [r6, #2336]	@ zero_extendqisi2
	ldr	r1, .L269+176
	mov	r0, r5
	bl	sprintf
.LVL250:
	add	r5, r5, r0
	.loc 3 60 0
	ldrh	r2, [r6, #2332]
	ldr	r1, .L269+180
	mov	r0, r5
	bl	sprintf
.LVL251:
	.loc 3 61 0
	ldr	r3, [r6, #2260]
	.loc 3 60 0
	add	r5, r5, r0
	.loc 3 61 0
	ldrh	r2, [r6, #2328]
	mov	r0, r5
	ldr	r1, .L269+184
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
.LVL252:
	add	r5, r5, r0
	.loc 3 62 0
	ldrh	r2, [r6, #2378]
	ldr	r1, .L269+188
	mov	r0, r5
	bl	sprintf
.LVL253:
	add	r5, r5, r0
	.loc 3 63 0
	ldrb	r2, [r6, #2382]	@ zero_extendqisi2
	ldr	r1, .L269+192
	mov	r0, r5
	bl	sprintf
.LVL254:
	add	r5, r5, r0
	.loc 3 64 0
	ldrh	r2, [r6, #2376]
	ldr	r1, .L269+196
	mov	r0, r5
	bl	sprintf
.LVL255:
	add	r5, r5, r0
	.loc 3 65 0
	ldrb	r2, [r6, #2384]	@ zero_extendqisi2
	ldr	r1, .L269+200
	mov	r0, r5
	bl	sprintf
.LVL256:
	add	r5, r5, r0
	.loc 3 66 0
	ldrh	r2, [r6, #2380]
	ldr	r1, .L269+204
	mov	r0, r5
	bl	sprintf
.LVL257:
	add	r5, r5, r0
	.loc 3 67 0
	ldrh	r2, [r6, #2570]
	ldr	r1, .L269+208
	mov	r0, r5
	bl	sprintf
.LVL258:
	add	r5, r5, r0
	.loc 3 68 0
	ldrb	r2, [r6, #2574]	@ zero_extendqisi2
	ldr	r1, .L269+212
	mov	r0, r5
	bl	sprintf
.LVL259:
	add	r5, r5, r0
	.loc 3 69 0
	ldrh	r2, [r6, #2568]
	ldr	r1, .L269+216
	mov	r0, r5
	bl	sprintf
.LVL260:
	add	r5, r5, r0
	.loc 3 70 0
	ldrb	r2, [r6, #2576]	@ zero_extendqisi2
	ldr	r1, .L269+220
	mov	r0, r5
	bl	sprintf
.LVL261:
	add	r5, r5, r0
	.loc 3 71 0
	ldrh	r2, [r6, #2572]
	ldr	r1, .L269+224
	mov	r0, r5
	bl	sprintf
.LVL262:
	.loc 3 72 0
	ldr	r3, [r6, #2692]
	.loc 3 71 0
	add	r5, r5, r0
	.loc 3 72 0
	ldr	r2, [r6, #2696]
	mov	r0, r5
	ldr	r1, .L269+228
	str	r3, [sp]
	ldr	r3, [r6, #2700]
	bl	sprintf
.LVL263:
	adds	r4, r5, r0
	.loc 3 73 0
	ldr	r2, [r6, #2688]
	ldr	r1, .L269+232
	mov	r0, r4
	bl	sprintf
.LVL264:
	add	r4, r4, r0
	.loc 3 74 0
	ldr	r2, [r6, #2712]
	ldr	r1, .L269+236
	mov	r0, r4
	bl	sprintf
.LVL265:
	add	r4, r4, r0
	.loc 3 75 0
	ldrh	r2, [r6, #3128]
	ldr	r1, .L269+240
	mov	r0, r4
	bl	sprintf
.LVL266:
	add	r4, r4, r0
	.loc 3 76 0
	ldrh	r2, [r6, #3130]
	ldr	r1, .L269+244
	mov	r0, r4
	bl	sprintf
.LVL267:
	add	r4, r4, r0
	.loc 3 77 0
	ldr	r2, [r6, #3132]
	b	.L270
.L271:
	.align	2
.L269:
	.word	.LANCHOR0
	.word	.LC2
	.word	.LC3
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.word	.LC7
	.word	.LC8
	.word	.LC9
	.word	.LC10
	.word	.LC11
	.word	.LC12
	.word	.LC13
	.word	.LC14
	.word	.LC15
	.word	.LC16
	.word	.LC17
	.word	.LC18
	.word	.LC19
	.word	.LC20
	.word	.LC21
	.word	.LC22
	.word	.LC23
	.word	.LC24
	.word	.LC25
	.word	.LC26
	.word	.LC27
	.word	.LC28
	.word	.LC29
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	.LC37
	.word	.LC38
	.word	.LC39
	.word	.LC40
	.word	.LC41
	.word	.LC42
	.word	.LC43
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	.LC50
	.word	.LC51
	.word	.LC52
	.word	.LC53
	.word	.LC54
	.word	.LC55
	.word	.LC56
	.word	.LC57
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.word	.LC61
	.word	.LC62
.L270:
	ldr	r1, .L272
	mov	r0, r4
	bl	sprintf
.LVL268:
	add	r4, r4, r0
	.loc 3 78 0
	ldrh	r2, [r6, #3136]
	ldr	r1, .L272+4
	mov	r0, r4
	bl	sprintf
.LVL269:
	add	r4, r4, r0
	.loc 3 79 0
	bl	GetFreeBlockMinEraseCount
.LVL270:
	ldr	r1, .L272+8
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL271:
	add	r4, r4, r0
	.loc 3 80 0
	ldrh	r0, [r6, #2276]
	bl	GetFreeBlockMaxEraseCount
.LVL272:
	ldr	r1, .L272+12
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL273:
	.loc 3 81 0
	ldr	r3, .L272+16
	.loc 3 80 0
	add	r4, r4, r0
	.loc 3 81 0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L258
.LVL274:
.L263:
	.loc 3 104 0
	subs	r0, r4, r7
	.loc 3 105 0
	add	sp, sp, #20
.LCFI36:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL275:
.L258:
.LCFI37:
	.loc 3 84 0
	ldrh	r3, [r6, #2568]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L260
	.loc 3 85 0
	ldr	r2, [r6, #2260]
	mov	r0, r4
	ldr	r1, .L272+20
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
.LVL276:
	add	r4, r4, r0
.L260:
.LBB284:
	.loc 3 87 0
	movs	r0, #0
	.loc 3 91 0
	ldr	r9, .L272+28
	.loc 3 87 0
	bl	List_get_gc_head_node
.LVL277:
	uxth	r3, r0
.LVL278:
	.loc 3 88 0
	movs	r5, #0
	.loc 3 89 0
	movw	fp, #65535
	.loc 3 91 0
	mov	r10, #6
.L262:
	.loc 3 89 0
	cmp	r3, fp
	beq	.L261
	.loc 3 91 0 discriminator 2
	ldr	r2, [r6, #2248]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, r9
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r6, #2252]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2260]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
.LVL279:
	.loc 3 88 0 discriminator 2
	adds	r5, r5, #1
	.loc 3 92 0 discriminator 2
	ldr	r3, [r6, #2252]
	.loc 3 88 0 discriminator 2
	cmp	r5, #16
	.loc 3 91 0 discriminator 2
	add	r4, r4, r0
	.loc 3 92 0 discriminator 2
	ldrh	r3, [r3, r8]
.LVL280:
	.loc 3 88 0 discriminator 2
	bne	.L262
.L261:
.LBE284:
.LBB285:
	.loc 3 96 0
	ldr	r2, [r6, #2252]
	.loc 3 97 0
	movs	r5, #0
	.loc 3 96 0
	ldr	r3, [r6, #2272]
.LVL281:
	.loc 3 98 0
	movw	r9, #65535
	.loc 3 100 0
	ldr	fp, .L272+32
	mov	r10, #6
	.loc 3 96 0
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L272+24
	muls	r3, r2, r3
	uxth	r3, r3
.LVL282:
.L264:
	.loc 3 98 0
	cmp	r3, r9
	beq	.L263
	.loc 3 100 0 discriminator 2
	ldr	r2, [r6, #2248]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, fp
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2252]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	.loc 3 97 0 discriminator 2
	adds	r5, r5, #1
	.loc 3 100 0 discriminator 2
	bl	sprintf
.LVL283:
	.loc 3 97 0 discriminator 2
	cmp	r5, #4
	.loc 3 100 0 discriminator 2
	add	r4, r4, r0
	.loc 3 97 0 discriminator 2
	beq	.L263
	.loc 3 101 0
	ldr	r3, [r6, #2252]
	ldrh	r3, [r3, r8]
.LVL284:
	b	.L264
.L273:
	.align	2
.L272:
	.word	.LC63
	.word	.LC64
	.word	.LC65
	.word	.LC66
	.word	.LANCHOR2
	.word	.LC67
	.word	-1431655765
	.word	.LC68
	.word	.LC69
.LBE285:
.LFE150:
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	1
	.global	rknand_proc_ftlread
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
.LFB151:
	.loc 3 114 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL285:
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI38:
	.loc 3 114 0
	mov	r5, r0
	.loc 3 120 0
	ldr	r2, .L275
	ldr	r1, .L275+4
	bl	sprintf
.LVL286:
	adds	r4, r5, r0
	.loc 3 122 0
	mov	r0, r4
	bl	FtlPrintInfo2buf
.LVL287:
	add	r0, r0, r4
	.loc 3 124 0
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.LVL288:
.L276:
	.align	2
.L275:
	.word	.LC70
	.word	.LC71
.LFE151:
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	1
	.global	GetSwlReplaceBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
.LFB246:
	.loc 2 2447 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI39:
	.pad #20
	sub	sp, sp, #20
.LCFI40:
	.loc 2 2456 0
	ldr	r4, .L302
	ldr	r2, [r4, #2488]
	ldr	r3, [r4, #2500]
	cmp	r2, r3
	bcs	.L278
	.loc 2 2459 0
	ldrh	r1, [r4, #16]
	.loc 2 2458 0
	movs	r3, #0
	.loc 2 2460 0
	ldr	r0, [r4, #2248]
	.loc 2 2458 0
	str	r3, [r4, #2480]
.L279:
	.loc 2 2459 0 discriminator 1
	cmp	r3, r1
	bcc	.L280
	.loc 2 2461 0
	ldr	r5, [r4, #2480]
	mov	r0, r5
	bl	__aeabi_uidiv
.LVL289:
	str	r0, [r4, #2488]
	.loc 2 2462 0
	ldr	r0, [r4, #2484]
	ldrh	r1, [r4, #68]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
.LVL290:
	str	r0, [r4, #2480]
.L281:
	.loc 2 2468 0
	ldr	r6, [r4, #2500]
	ldr	r7, [r4, #2488]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L286
	.loc 2 2468 0 is_stmt 0 discriminator 1
	ldr	r2, [r4, #2496]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L286
.LVL291:
.L289:
	.loc 2 2469 0 is_stmt 1
	movw	r5, #65535
.L287:
	.loc 2 2518 0
	mov	r0, r5
	add	sp, sp, #20
.LCFI41:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL292:
.L280:
.LCFI42:
	.loc 2 2460 0 discriminator 3
	ldrh	r2, [r0, r3, lsl #1]
	.loc 2 2459 0 discriminator 3
	adds	r3, r3, #1
	.loc 2 2460 0 discriminator 3
	ldr	r5, [r4, #2480]
	add	r2, r2, r5
	str	r2, [r4, #2480]
	b	.L279
.L278:
	.loc 2 2463 0
	ldr	r3, [r4, #2496]
	cmp	r2, r3
	bls	.L281
	.loc 2 2464 0
	adds	r3, r3, #1
	str	r3, [r4, #2496]
	.loc 2 2465 0
	movs	r3, #0
.L283:
	.loc 2 2465 0 is_stmt 0 discriminator 1
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	bcs	.L281
	.loc 2 2466 0 is_stmt 1 discriminator 3
	ldr	r1, [r4, #2248]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	.loc 2 2465 0 discriminator 3
	adds	r3, r3, #1
	b	.L283
.L286:
	.loc 2 2471 0
	ldrh	r0, [r4, #2276]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
.LVL293:
	.loc 2 2472 0
	add	r3, r6, #64
	.loc 2 2471 0
	mov	r9, r0
.LVL294:
	.loc 2 2472 0
	cmp	r0, r3
	bcs	.L288
	.loc 2 2472 0 is_stmt 0 discriminator 1
	cmp	r6, #30
	bhi	.L289
.L288:
	.loc 2 2476 0 is_stmt 1
	ldr	r3, [r4, #2256]
.LVL295:
	cmp	r3, #0
	beq	.L289
	.loc 2 2483 0
	ldr	ip, [r4, #2252]
	.loc 2 2484 0
	movw	r1, #65535
.LVL296:
	ldr	r8, [r4, #2248]
	mov	r2, r1
	.loc 2 2483 0
	ldr	r10, .L302+8
	.loc 2 2479 0
	mov	lr, r1
	.loc 2 2495 0
	mov	fp, #6
.L290:
	.loc 2 2479 0
	ldrh	r0, [r3]
	cmp	r0, lr
	bne	.L293
	mov	r5, r2
.L292:
	.loc 2 2498 0
	movw	r3, #65535
.LVL297:
	cmp	r5, r3
	beq	.L289
	.loc 2 2500 0
	ldrh	r10, [r8, r5, lsl #1]
.LVL298:
	lsl	fp, r5, #1
	.loc 2 2501 0
	cmp	r6, r10
	bcs	.L294
	.loc 2 2503 0
	bl	GetFreeBlockMinEraseCount
.LVL299:
	cmp	r6, r0
	.loc 2 2504 0
	it	cc
	strcc	r1, [r4, #2500]
.L294:
	.loc 2 2506 0
	cmp	r7, r10
	bls	.L289
	.loc 2 2506 0 is_stmt 0 discriminator 1
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L289
	.loc 2 2508 0 is_stmt 1
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L295
	.loc 2 2508 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #2496]
	add	r10, r10, #768
.LVL300:
	cmp	r10, r3
	bcs	.L289
.LVL301:
.L295:
	.loc 2 2510 0 is_stmt 1
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
.LVL302:
	ldr	r0, .L302+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2496]
	bl	printk
.LVL303:
	.loc 2 2511 0
	movs	r3, #1
	str	r3, [r4, #3140]
	.loc 2 2512 0
	b	.L287
.LVL304:
.L293:
	.loc 2 2481 0
	ldrh	r5, [r3, #4]
	cbz	r5, .L291
	.loc 2 2483 0
	sub	r3, r3, ip
.LVL305:
	asrs	r3, r3, #1
	mul	r3, r10, r3
	uxth	r5, r3
	.loc 2 2484 0
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L292
	.loc 2 2489 0
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	.loc 2 2483 0
	movhi	r2, r5
.LVL306:
.L291:
	.loc 2 2495 0
	mla	r3, fp, r0, ip
	b	.L290
.L303:
	.align	2
.L302:
	.word	.LANCHOR0
	.word	.LC72
	.word	-1431655765
.LFE246:
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	1
	.global	free_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
.LFB247:
	.loc 2 2522 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL307:
	.loc 2 2523 0
	movw	r2, #65535
	.loc 2 2522 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI43:
	.loc 2 2523 0
	cmp	r0, r2
	beq	.L305
.LBB288:
.LBB289:
	.loc 2 2525 0
	ldr	r2, .L306
	movs	r1, #0
	ldr	r2, [r2, #2260]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	.loc 2 2526 0
	bl	INSERT_FREE_LIST
.LVL308:
.L305:
.LBE289:
.LBE288:
	.loc 2 2528 0
	movs	r0, #0
.LVL309:
	pop	{r3, pc}
.L307:
	.align	2
.L306:
	.word	.LANCHOR0
.LFE247:
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	1
	.global	get_new_active_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
.LFB250:
	.loc 2 2655 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL310:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI44:
	.loc 2 2659 0
	movw	r3, #65535
	ldrh	r2, [r0]
	.loc 2 2655 0
	mov	r4, r0
	.loc 2 2659 0
	cmp	r2, r3
	bne	.L309
	.loc 2 2659 0 is_stmt 0 discriminator 1
	movw	r2, #2659
	ldr	r1, .L322
	ldr	r0, .L322+4
	bl	printk
.LVL311:
.L309:
	.loc 2 2660 0 is_stmt 1
	ldr	r5, .L322+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #78]
	cmp	r2, r3
	bne	.L310
	.loc 2 2660 0 is_stmt 0 discriminator 1
	movw	r2, #2660
	ldr	r1, .L322
	ldr	r0, .L322+4
	bl	printk
.LVL312:
.L310:
	.loc 2 2661 0 is_stmt 1
	ldrh	r3, [r4, #4]
	cbnz	r3, .L311
	.loc 2 2661 0 is_stmt 0 discriminator 1
	movw	r2, #2661
	ldr	r1, .L322
	ldr	r0, .L322+4
	bl	printk
.LVL313:
.L311:
	.loc 2 2663 0 is_stmt 1
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	.loc 2 2662 0
	movs	r3, #0
	.loc 2 2666 0
	ldrh	r1, [r5, #8]
	.loc 2 2664 0
	movw	r6, #65535
	.loc 2 2662 0
	strb	r3, [r4, #10]
	.loc 2 2663 0
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
.LVL314:
	.loc 2 2668 0
	mov	r2, r3
.L312:
	.loc 2 2664 0
	cmp	r0, r6
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L314
	.loc 2 2673 0
	ldrh	r2, [r4, #4]
	.loc 2 2672 0
	ldrh	r6, [r4, #2]
	.loc 2 2673 0
	subs	r2, r2, #1
	uxth	r2, r2
	.loc 2 2672 0
	orr	r6, r6, r0, lsl #10
	.loc 2 2680 0
	movw	r0, #65535
.LVL315:
	.loc 2 2673 0
	strh	r2, [r4, #4]	@ movhi
.L316:
	.loc 2 2675 0
	adds	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2676 0
	cmp	r1, r3
	.loc 2 2677 0
	itttt	eq
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2678 0
	moveq	r3, #0
	.loc 2 2680 0
	add	r7, r3, #8
	ldrh	r7, [r4, r7, lsl #1]
	cmp	r7, r0
	beq	.L316
	strb	r3, [r4, #6]
	.loc 2 2682 0
	ldrh	r1, [r4, #2]
	ldrh	r3, [r5, #78]
	cmp	r1, r3
	bne	.L308
	.loc 2 2682 0 is_stmt 0 discriminator 1
	cbz	r2, .L308
	.loc 2 2682 0 discriminator 2
	movw	r2, #2682
	ldr	r1, .L322
	ldr	r0, .L322+4
	bl	printk
.LVL316:
.L308:
	.loc 2 2684 0 is_stmt 1
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.LVL317:
.L314:
	.loc 2 2665 0
	adds	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2666 0
	cmp	r3, r1
	.loc 2 2665 0
	strb	r3, [r4, #6]
	.loc 2 2667 0
	itttt	eq
	ldrheq	r3, [r4, #2]
	.loc 2 2668 0
	strbeq	r2, [r4, #6]
	.loc 2 2667 0
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2670 0
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L312
.L323:
	.align	2
.L322:
	.word	.LANCHOR1+137
	.word	.LC1
	.word	.LANCHOR0
.LFE250:
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	1
	.global	FtlGcBufInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
.LFB253:
	.file 5 "drivers/rkflash/rksftl/sftl_gc.c"
	.loc 5 22 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	.loc 5 25 0
	ldr	r3, .L333
	.loc 5 30 0
	mov	ip, #20
	.loc 5 22 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
.LCFI45:
	.loc 5 27 0
	mov	r9, #12
	movs	r6, #1
	.loc 5 25 0
	movs	r2, #0
	.loc 5 29 0
	mov	r7, r3
	.loc 5 25 0
	str	r2, [r3, #3144]
.L325:
	.loc 5 26 0 discriminator 1
	ldrh	r1, [r3, #8]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L328
	.loc 5 37 0 discriminator 1
	ldr	r7, .L333
	.loc 5 35 0 discriminator 1
	mov	ip, #12
	movs	r6, #0
.L329:
	.loc 5 34 0 discriminator 1
	ldr	r2, [r3, #3164]
	cmp	r1, r2
	bcc	.L332
	.loc 5 39 0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L328:
	uxth	r2, r2
	.loc 5 27 0 discriminator 3
	ldr	r8, [r3, #3148]
	mul	lr, r9, r2
	add	r1, r8, lr
	str	r6, [r1, #8]
	.loc 5 28 0 discriminator 3
	ldrh	r1, [r3, #86]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r0, r1, #3
	ldr	r1, [r3, #3152]
	add	r1, r1, r0
	str	r1, [r8, lr]
	.loc 5 29 0 discriminator 3
	ldr	r5, [r7, #3156]
	ldr	r0, [r3, #3148]
	ldrh	r1, [r3, #88]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r1, r1, #3
	add	r8, r0, lr
	add	r1, r1, r5
	str	r1, [r8, #4]
	.loc 5 30 0 discriminator 3
	ldr	r1, [r3, #3160]
	mla	r2, ip, r2, r1
	ldr	r1, [r0, lr]
	str	r1, [r2, #8]
	.loc 5 31 0 discriminator 3
	ldr	r1, [r8, #4]
	str	r1, [r2, #12]
	mov	r2, r4
	b	.L325
.L332:
	.loc 5 35 0 discriminator 3
	mul	r0, ip, r1
	ldr	r4, [r3, #3148]
	.loc 5 36 0 discriminator 3
	ldr	r5, [r3, #3152]
	.loc 5 35 0 discriminator 3
	adds	r2, r4, r0
	str	r6, [r2, #8]
	.loc 5 36 0 discriminator 3
	ldrh	r2, [r3, #86]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	add	r2, r2, r5
	str	r2, [r4, r0]
	.loc 5 37 0 discriminator 3
	ldr	r2, [r3, #3148]
	ldr	r4, [r7, #3156]
	add	r0, r0, r2
	ldrh	r2, [r3, #88]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	.loc 5 34 0 discriminator 3
	adds	r1, r1, #1
	.loc 5 37 0 discriminator 3
	add	r2, r2, r4
	.loc 5 34 0 discriminator 3
	uxth	r1, r1
	.loc 5 37 0 discriminator 3
	str	r2, [r0, #4]
	b	.L329
.L334:
	.align	2
.L333:
	.word	.LANCHOR0
.LFE253:
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	1
	.global	FtlGcBufFree
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
.LFB254:
	.loc 5 42 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL318:
	.loc 5 47 0
	ldr	r3, .L342
	.loc 5 42 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI46:
	.loc 5 46 0
	movs	r5, #0
	.loc 5 48 0
	mov	r10, #20
	mov	lr, #12
	.loc 5 49 0
	mov	r8, r5
	.loc 5 47 0
	ldr	ip, [r3, #3164]
	.loc 5 48 0
	ldr	r6, [r3, #3148]
.L336:
	uxth	r3, r5
	.loc 5 46 0 discriminator 1
	cmp	r1, r3
	bls	.L335
	.loc 5 48 0
	mla	r4, r10, r3, r0
	movs	r2, #0
.L337:
	uxth	r3, r2
	.loc 5 47 0 discriminator 1
	cmp	ip, r3
	bls	.L338
	.loc 5 48 0
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L337
	.loc 5 49 0
	str	r8, [r9, #8]
.L338:
	adds	r5, r5, #1
	b	.L336
.L335:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L343:
	.align	2
.L342:
	.word	.LANCHOR0
.LFE254:
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	1
	.global	FtlGcBufAlloc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
.LFB255:
	.loc 5 57 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL319:
	.loc 5 62 0
	ldr	r3, .L351
	.loc 5 63 0
	mov	ip, #12
	.loc 5 57 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
.LCFI47:
	.loc 5 61 0
	movs	r4, #0
	.loc 5 64 0
	mov	lr, #1
	.loc 5 65 0
	mov	r8, #20
	.loc 5 62 0
	ldr	r5, [r3, #3164]
	.loc 5 63 0
	ldr	r6, [r3, #3148]
.L345:
	uxth	r2, r4
	.loc 5 61 0 discriminator 1
	cmp	r1, r2
	bhi	.L349
	.loc 5 71 0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L349:
	mov	r9, #0
.L346:
	uxth	r3, r9
	.loc 5 62 0 discriminator 1
	cmp	r5, r3
	bls	.L347
	.loc 5 63 0
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L346
	.loc 5 65 0
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	.loc 5 64 0
	str	lr, [r3, #8]
	.loc 5 65 0
	str	r7, [r2, #8]
	.loc 5 66 0
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L347:
	adds	r4, r4, #1
	b	.L345
.L352:
	.align	2
.L351:
	.word	.LANCHOR0
.LFE255:
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	1
	.global	IsBlkInGcList
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
.LFB256:
	.loc 5 74 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL320:
	.loc 5 78 0
	ldr	r2, .L358
	ldr	r3, [r2, #3168]
	ldrh	r2, [r2, #3172]
	add	r2, r3, r2, lsl #1
.L354:
	.loc 5 77 0 discriminator 1
	cmp	r3, r2
	bne	.L356
	.loc 5 81 0
	movs	r0, #0
.LVL321:
	bx	lr
.LVL322:
.L356:
	.loc 5 78 0
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L354
	.loc 5 79 0
	movs	r0, #1
.LVL323:
	.loc 5 82 0
	bx	lr
.L359:
	.align	2
.L358:
	.word	.LANCHOR0
.LFE256:
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	1
	.global	FtlGcUpdatePage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
.LFB257:
	.loc 5 85 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL324:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI48:
	.loc 5 85 0
	mov	r5, r0
	.loc 5 88 0
	ubfx	r0, r0, #10, #16
.LVL325:
	.loc 5 85 0
	mov	r6, r1
	mov	r7, r2
	.loc 5 91 0
	movs	r4, #0
	.loc 5 88 0
	bl	P2V_block_in_plane
.LVL326:
	.loc 5 91 0
	ldr	r3, .L364
	ldrh	r1, [r3, #3172]
	.loc 5 92 0
	ldr	r2, [r3, #3168]
.LVL327:
.L361:
	uxth	ip, r4
.LVL328:
	.loc 5 91 0 discriminator 1
	cmp	ip, r1
	bcc	.L363
	.loc 5 96 0
	bne	.L362
	.loc 5 97 0
	strh	r0, [r2, ip, lsl #1]	@ movhi
.LVL329:
	.loc 5 98 0
	ldrh	r0, [r3, #3172]
.LVL330:
	adds	r0, r0, #1
	strh	r0, [r3, #3172]	@ movhi
	b	.L362
.LVL331:
.L363:
	adds	r4, r4, #1
	.loc 5 92 0
	add	ip, r2, r4, lsl #1
.LVL332:
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L361
.LVL333:
.L362:
	.loc 5 101 0
	ldrh	r2, [r3, #3180]
	movs	r0, #12
.LVL334:
	muls	r0, r2, r0
	ldr	r2, [r3, #3176]
	adds	r1, r2, r0
	str	r6, [r1, #4]
	.loc 5 102 0
	str	r7, [r1, #8]
	.loc 5 103 0
	str	r5, [r2, r0]
	.loc 5 107 0
	ldrh	r2, [r3, #3180]
	adds	r2, r2, #1
	strh	r2, [r3, #3180]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.LVL335:
.L365:
	.align	2
.L364:
	.word	.LANCHOR0
.LFE257:
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	1
	.global	FtlGcRefreshBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
.LFB264:
	.loc 5 345 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL336:
	push	{r4, lr}
	.save {r4, lr}
.LCFI49:
	.loc 5 347 0
	mov	r1, r0
	.loc 5 345 0
	mov	r4, r0
	.loc 5 347 0
	ldr	r0, .L369
.LVL337:
	bl	printk
.LVL338:
	.loc 5 349 0
	ldr	r3, .L369+4
	ldrh	r0, [r3, #3182]
	cmp	r4, r0
	beq	.L367
	.loc 5 349 0 is_stmt 0 discriminator 1
	ldrh	r1, [r3, #3184]
	cmp	r4, r1
	beq	.L367
	.loc 5 351 0 is_stmt 1
	movw	r2, #65535
	cmp	r0, r2
	bne	.L368
	.loc 5 352 0
	strh	r4, [r3, #3182]	@ movhi
.L367:
	.loc 5 356 0
	movs	r0, #0
	pop	{r4, pc}
.LVL339:
.L368:
	.loc 5 353 0
	cmp	r1, r2
	.loc 5 354 0
	it	eq
	strheq	r4, [r3, #3184]	@ movhi
	b	.L367
.L370:
	.align	2
.L369:
	.word	.LC73
	.word	.LANCHOR0
.LFE264:
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	1
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
.LFB265:
	.loc 5 359 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL340:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI50:
	.loc 5 359 0
	mov	r5, r0
	.loc 5 364 0
	ldr	r4, .L375
	.loc 5 361 0
	bl	P2V_block_in_plane
.LVL341:
	.loc 5 364 0
	mov	r2, r5
	.loc 5 361 0
	mov	r6, r0
	.loc 5 364 0
	ldrh	r1, [r4, #3186]
	ldr	r0, .L375+4
	bl	printk
.LVL342:
	.loc 5 366 0
	mov	r0, r6
	bl	FtlGcRefreshBlock
.LVL343:
	.loc 5 368 0
	ldrh	r3, [r4, #3186]
	movs	r2, #0
	.loc 5 369 0
	addw	r0, r4, #3188
.L372:
	.loc 5 368 0 discriminator 1
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L374
	.loc 5 372 0
	cmp	r3, #15
	.loc 5 373 0
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3186]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3188]	@ movhi
	b	.L373
.L374:
	adds	r2, r2, #1
	.loc 5 369 0
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L372
.L373:
	.loc 5 375 0
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.LVL344:
.L376:
	.align	2
.L375:
	.word	.LANCHOR0
	.word	.LC74
.LFE265:
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	1
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
.LFB266:
	.loc 5 379 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI51:
	.loc 5 380 0
	ldr	r4, .L383
	ldrh	r3, [r4, #3186]
	cbz	r3, .L378
	.loc 5 380 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #3182]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L378
	.loc 5 381 0 is_stmt 1
	ldrh	r2, [r4, #3222]
	cmp	r2, r3
	.loc 5 382 0
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #3222]	@ movhi
.LBB292:
.LBB293:
	.loc 5 383 0
	ldrh	r3, [r4, #3222]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #3188]
	bl	P2V_block_in_plane
.LVL345:
	bl	FtlGcRefreshBlock
.LVL346:
	.loc 5 384 0
	ldrh	r3, [r4, #3222]
	adds	r3, r3, #1
	strh	r3, [r4, #3222]	@ movhi
.L378:
.LBE293:
.LBE292:
	.loc 5 387 0
	movs	r0, #0
	pop	{r4, pc}
.L384:
	.align	2
.L383:
	.word	.LANCHOR0
.LFE266:
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	1
	.global	FlashReadPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
.LFB272:
	.loc 1 44 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL347:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI52:
	mov	r9, r1
	.loc 1 46 0
	ldr	r5, .L403
	mov	r4, r0
	.loc 1 50 0
	movs	r6, #0
.LVL348:
	.loc 1 44 0
	.pad #20
	sub	sp, sp, #20
.LCFI53:
	.loc 1 51 0
	ldr	r10, .L403+4
	.loc 1 46 0
	ldrh	r7, [r5, #12]
.LVL349:
	.loc 1 51 0
	ldr	fp, .L403+8
	.loc 1 53 0
	ldr	r8, .L403+12
	str	r5, [sp, #4]
.LVL350:
.L386:
	.loc 1 50 0 discriminator 1
	cmp	r6, r9
	bne	.L393
	.loc 1 71 0
	movs	r0, #0
.LVL351:
	add	sp, sp, #20
.LCFI54:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL352:
.L393:
.LCFI55:
	.loc 1 51 0
	ldr	r3, [r4, #8]
	cbz	r3, .L387
	.loc 1 51 0 is_stmt 0 discriminator 2
	ldr	r3, [r4, #12]
	cbnz	r3, .L388
.L387:
	.loc 1 51 0 discriminator 3
	movs	r2, #51
.LVL353:
	mov	r1, r10
	mov	r0, fp
.LVL354:
	bl	printk
.LVL355:
.L388:
	.loc 1 52 0 is_stmt 1
	add	r2, sp, #8
.LVL356:
	add	r1, sp, #12
	mov	r0, r4
.LVL357:
	bl	l2p_addr_tran
.LVL358:
	.loc 1 53 0
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #12]
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	ldr	r5, [r8, #12]
	blx	r5
.LVL359:
	.loc 1 58 0
	ldr	r3, [sp, #4]
	.loc 1 57 0
	str	r0, [r4]
	.loc 1 58 0
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L390
	.loc 1 59 0
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #12]
	adds	r3, r3, #8
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r5, [r8, #12]
	add	r1, r1, r7
	blx	r5
.LVL360:
	.loc 1 63 0
	adds	r0, r0, #1
	beq	.L391
	.loc 1 64 0 discriminator 1
	ldr	r3, [r4, #12]
	.loc 1 63 0 discriminator 1
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L390
	.loc 1 64 0
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L390
	.loc 1 65 0
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L390
.L391:
	.loc 1 67 0
	mov	r3, #-1
	str	r3, [r4]
.L390:
	.loc 1 50 0 discriminator 2
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L386
.L404:
	.align	2
.L403:
	.word	g_nand_phy_info
	.word	.LANCHOR1+156
	.word	.LC1
	.word	g_nand_ops
.LFE272:
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	1
	.global	FtlLoadFactoryBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
.LFB180:
	.loc 4 279 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI56:
	.loc 4 289 0
	movs	r6, #0
.LVL361:
	.loc 4 286 0
	ldr	r4, .L415
	.loc 4 301 0
	movw	r10, #61664
	.loc 4 286 0
	ldr	r3, [r4, #2244]
	add	r7, r4, #138
	.loc 4 287 0
	ldr	r8, [r4, #3244]
	.loc 4 294 0
	addw	r9, r4, #3224
	.loc 4 286 0
	str	r3, [r4, #3232]
	.loc 4 287 0
	str	r8, [r4, #3236]
.LVL362:
.L406:
	.loc 4 289 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r6, r3
	bcc	.L411
	.loc 4 314 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL363:
.L411:
	.loc 4 291 0
	ldrh	r5, [r4, #74]
.LVL364:
	.loc 4 290 0
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.LVL365:
.L408:
	.loc 4 291 0 discriminator 2
	ldrh	r3, [r4, #74]
	subs	r5, r5, #1
.LVL366:
	uxth	r5, r5
.LVL367:
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L409
	.loc 4 292 0
	mla	r3, r6, r3, r5
	.loc 4 294 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	.loc 4 292 0
	lsls	r3, r3, #10
.LVL368:
	.loc 4 293 0
	str	r3, [r4, #3228]
	.loc 4 294 0
	bl	FlashReadPages
.LVL369:
	.loc 4 295 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	beq	.L408
	.loc 4 301 0
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L408
	.loc 4 303 0
	strh	r5, [r7]	@ movhi
.L409:
	.loc 4 289 0 discriminator 2
	adds	r6, r6, #1
	b	.L406
.L416:
	.align	2
.L415:
	.word	.LANCHOR0
.LFE180:
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	1
	.global	FtlGetLastWrittenPage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
.LFB186:
	.loc 2 47 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL370:
	ldr	r3, .L425
	.loc 2 51 0
	cmp	r1, #1
	.loc 2 47 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI57:
	.loc 2 57 0
	lsl	r7, r0, #10
	.loc 2 47 0
	.pad #88
	sub	sp, sp, #88
.LCFI58:
	.loc 2 58 0
	mov	r2, r1
	.loc 2 47 0
	mov	r6, r1
	.loc 2 51 0
	it	eq
	ldrheq	r4, [r3, #80]
	.loc 2 58 0
	mov	r1, #1
.LVL371:
	.loc 2 51 0
	it	ne
	ldrhne	r4, [r3, #78]
	.loc 2 58 0
	add	r0, sp, #4
.LVL372:
	.loc 2 53 0
	ldr	r3, .L425+4
	.loc 2 56 0
	subs	r4, r4, #1
	sxth	r5, r4
.LVL373:
	.loc 2 53 0
	str	r3, [sp, #12]
	.loc 2 54 0
	add	r3, sp, #24
	str	r3, [sp, #16]
	.loc 2 57 0
	orr	r3, r5, r7
	str	r3, [sp, #8]
	.loc 2 58 0
	bl	FlashReadPages
.LVL374:
	.loc 2 59 0
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L420
	mov	r8, #0
.LVL375:
.L421:
	.loc 2 60 0
	cmp	r8, r5
	ble	.L424
.L420:
	.loc 2 73 0
	mov	r0, r5
	add	sp, sp, #88
.LCFI59:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL376:
.L424:
.LCFI60:
	.loc 2 61 0
	add	r4, r8, r5
	.loc 2 63 0
	mov	r2, r6
	.loc 2 61 0
	add	r4, r4, r4, lsr #31
	.loc 2 63 0
	movs	r1, #1
	add	r0, sp, #4
	.loc 2 61 0
	asrs	r4, r4, #1
	.loc 2 62 0
	sxth	r3, r4
	orrs	r3, r3, r7
	str	r3, [sp, #8]
	.loc 2 63 0
	bl	FlashReadPages
.LVL377:
	.loc 2 65 0
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L422
	.loc 2 65 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L422
	.loc 2 66 0 is_stmt 1
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L421
.L422:
	.loc 2 68 0
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L421
.L426:
	.align	2
.L425:
	.word	.LANCHOR0
	.word	ftl_temp_buf
.LFE186:
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	1
	.global	FlashProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
.LFB273:
	.loc 1 74 0
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL378:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI61:
	.pad #44
	sub	sp, sp, #44
.LCFI62:
	.loc 1 74 0
	str	r3, [sp]
	mov	fp, r1
	mov	r9, r2
	mov	r4, r0
	.loc 1 76 0
	ldr	r3, .L457
.LVL379:
	.loc 1 80 0
	mov	r5, r0
.LVL380:
	.loc 1 83 0
	ldr	r8, .L457+28
	.loc 1 80 0
	movs	r6, #0
	.loc 1 76 0
	ldrh	r10, [r3, #12]
.LVL381:
	str	r3, [sp, #4]
.LVL382:
.L428:
	.loc 1 80 0 discriminator 1
	cmp	r6, fp
	bne	.L436
	.loc 1 101 0
	ldr	r3, [sp]
.LVL383:
	cmp	r3, #0
	bne	.L443
.L456:
	.loc 1 131 0
	movs	r0, #0
	add	sp, sp, #44
.LCFI63:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL384:
.L436:
.LCFI64:
	.loc 1 81 0
	ldr	r3, [r5, #8]
	cbz	r3, .L429
	.loc 1 81 0 is_stmt 0 discriminator 2
	ldr	r3, [r5, #12]
	cbnz	r3, .L430
.L429:
	.loc 1 81 0 discriminator 3
	movs	r2, #81
	ldr	r1, .L457+4
	ldr	r0, .L457+8
	bl	printk
.LVL385:
.L430:
	.loc 1 82 0 is_stmt 1
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r5
	bl	l2p_addr_tran
.LVL386:
	.loc 1 83 0
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	ldr	r7, [r8, #8]
	blx	r7
.LVL387:
	.loc 1 88 0
	cbnz	r0, .L431
	.loc 1 87 0
	str	r0, [r5]
.L432:
	.loc 1 91 0
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L434
	.loc 1 92 0
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	adds	r3, r3, #8
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r7, [r8, #8]
	add	r1, r1, r10
	blx	r7
.LVL388:
	.loc 1 96 0
	cbz	r0, .L434
	.loc 1 97 0
	mov	r3, #-1
	str	r3, [r5]
.L434:
	.loc 1 80 0 discriminator 2
	adds	r6, r6, #1
	adds	r5, r5, #20
	b	.L428
.L431:
	.loc 1 89 0
	mov	r3, #-1
	str	r3, [r5]
	b	.L432
.LVL389:
.L441:
.LBB294:
	.loc 1 106 0
	movs	r3, #0
.LVL390:
	.loc 1 112 0
	mov	r2, r9
	.loc 1 106 0
	str	r3, [r7]
	.loc 1 112 0
	movs	r1, #1
	.loc 1 107 0
	str	r3, [r8, #3248]
	.loc 1 112 0
	add	r0, sp, #20
	.loc 1 108 0
	ldr	r3, [r4, #4]
	.loc 1 109 0
	str	r7, [sp, #28]
	.loc 1 110 0
	str	r10, [sp, #32]
	.loc 1 108 0
	str	r3, [sp, #24]
	.loc 1 112 0
	bl	FlashReadPages
.LVL391:
	.loc 1 113 0
	ldr	fp, [sp, #20]
.LVL392:
	cmp	fp, #-1
	bne	.L438
	.loc 1 114 0
	ldr	r1, [r4, #4]
	ldr	r0, .L457+12
	bl	printk
.LVL393:
	.loc 1 115 0
	str	fp, [r4]
.L438:
	.loc 1 117 0
	ldr	r3, [r4, #12]
	cbz	r3, .L439
	.loc 1 118 0
	ldr	r2, [r3]
	ldr	r3, [r8, #3248]
	cmp	r2, r3
	beq	.L439
	.loc 1 119 0
	ldr	r1, [r4, #4]
	ldr	r0, .L457+16
	bl	printk
.LVL394:
	.loc 1 120 0
	mov	r3, #-1
	str	r3, [r4]
.L439:
	.loc 1 123 0
	ldr	r3, [r4, #8]
	cbz	r3, .L440
	.loc 1 124 0
	ldr	r2, [r3]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L440
	.loc 1 125 0
	ldr	r1, [r4, #4]
	ldr	r0, .L457+20
	bl	printk
.LVL395:
	.loc 1 126 0
	mov	r3, #-1
	str	r3, [r4]
.L440:
.LBE294:
	.loc 1 103 0 discriminator 2
	adds	r5, r5, #1
	adds	r4, r4, #20
.LVL396:
.L437:
	.loc 1 103 0 is_stmt 0 discriminator 1
	cmp	r6, r5
	bne	.L441
	b	.L456
.LVL397:
.L443:
.LBB295:
	.loc 1 107 0 is_stmt 1
	ldr	r8, .L457+32
.LBE295:
	movs	r5, #0
.LBB296:
	.loc 1 106 0
	ldr	r7, .L457+24
	.loc 1 110 0
	add	r10, r8, #3248
.LVL398:
	b	.L437
.L458:
	.align	2
.L457:
	.word	g_nand_phy_info
	.word	.LANCHOR1+171
	.word	.LC1
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	check_buf
	.word	g_nand_ops
	.word	.LANCHOR0
.LBE296:
.LFE273:
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	1
	.global	FlashEraseBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
.LFB274:
	.loc 1 134 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL399:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
.LCFI65:
.LVL400:
	.loc 1 134 0
	mov	r8, r2
	.loc 1 137 0
	ldr	r5, .L470
.LVL401:
	mov	r4, r0
	.loc 1 140 0
	movs	r6, #0
.LVL402:
	.loc 1 142 0
	ldr	r7, .L470+4
	.loc 1 137 0
	ldrh	r9, [r5, #12]
.LVL403:
.L460:
	.loc 1 140 0 discriminator 1
	cmp	r6, r8
	bne	.L466
	.loc 1 153 0
	movs	r0, #0
.LVL404:
	add	sp, sp, #12
.LCFI66:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.LVL405:
.L466:
.LCFI67:
	.loc 1 141 0
	add	r1, sp, #4
	mov	r2, sp
	mov	r0, r4
.LVL406:
	bl	l2p_addr_tran
.LVL407:
	.loc 1 142 0
	ldr	r3, [r7, #4]
	ldr	r1, [sp, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	blx	r3
.LVL408:
	.loc 1 144 0
	cbnz	r0, .L461
	.loc 1 143 0
	str	r0, [r4]
.L462:
	.loc 1 146 0
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L464
	.loc 1 147 0
	ldr	r1, [sp, #4]
	ldr	r3, [r7, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r1, r1, r9
	blx	r3
.LVL409:
	.loc 1 148 0
	cbz	r0, .L464
	.loc 1 149 0
	mov	r3, #-1
	str	r3, [r4]
.L464:
	.loc 1 140 0 discriminator 2
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L460
.L461:
	.loc 1 145 0
	mov	r3, #-1
	str	r3, [r4]
	b	.L462
.L471:
	.align	2
.L470:
	.word	g_nand_phy_info
	.word	g_nand_ops
.LFE274:
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	1
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
.LFB190:
	.loc 2 98 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL410:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI68:
	.loc 2 98 0
	mov	r5, r0
.LBB297:
.LBB298:
	.loc 2 94 0
	ldr	r4, .L481
.LBE298:
.LBE297:
	.loc 2 99 0
	ldrh	r3, [r4, #194]
	cmp	r3, #1024
	beq	.L472
.LBB299:
	.loc 2 101 0
	cbz	r1, .L474
.LBB300:
	.loc 2 102 0
	bl	P2V_block_in_plane
.LVL411:
	mov	r6, r0
	.loc 2 103 0
	ldr	r0, [r4, #3760]
	lsls	r3, r5, #10
	.loc 2 104 0
	movs	r2, #1
	mov	r1, r2
	.loc 2 103 0
	str	r3, [r0, #4]
	.loc 2 104 0
	bl	FlashEraseBlocks
.LVL412:
	.loc 2 105 0
	ldr	r2, [r4, #2248]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	.loc 2 106 0
	ldr	r3, [r4, #2492]
	adds	r3, r3, #1
	str	r3, [r4, #2492]
.LVL413:
.L474:
.LBE300:
	.loc 2 108 0
	ldrh	r3, [r4, #194]
	adds	r3, r3, #1
	strh	r3, [r4, #194]	@ movhi
	.loc 2 109 0
	ldrh	r3, [r4, #192]
	add	r2, r4, r3, lsl #1
	.loc 2 110 0
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 109 0
	strh	r5, [r2, #196]	@ movhi
	.loc 2 110 0
	strh	r3, [r4, #192]	@ movhi
.L472:
	pop	{r4, r5, r6, pc}
.LVL414:
.L482:
	.align	2
.L481:
	.word	.LANCHOR0
.LBE299:
.LFE190:
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	1
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
.LFB159:
	.loc 3 528 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL415:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI69:
	.loc 3 539 0
	mov	r10, #0
	.loc 3 538 0
	ldr	r4, .L528
	.loc 3 528 0
	mov	r7, r1
	.loc 3 536 0
	mov	r6, r10
	.loc 3 537 0
	mov	r5, r10
.LVL416:
	.loc 3 540 0
	mov	r9, #20
	.loc 3 528 0
	.pad #20
	sub	sp, sp, #20
.LCFI70:
	.loc 3 551 0
	mov	fp, r4
	.loc 3 528 0
	str	r0, [sp]
	.loc 3 538 0
	str	r0, [r4, #3764]
.L484:
	.loc 3 539 0 discriminator 1
	ldrh	r1, [r4, #8]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L489
	.loc 3 559 0
	cmp	r6, #0
	beq	.L483
	.loc 3 563 0
	mov	r8, #0
	.loc 3 564 0
	mov	r9, #20
	.loc 3 562 0
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3760]
.LVL417:
	bl	FlashEraseBlocks
.LVL418:
.L492:
	.loc 3 563 0 discriminator 1
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L494
	.loc 3 571 0
	cmp	r7, #0
	beq	.L511
	.loc 3 573 0
	ldrh	r3, [r4, #80]
.LVL419:
	.loc 3 572 0
	mov	r10, #1
.LVL420:
	.loc 3 573 0
	str	r3, [sp, #4]
	.loc 3 574 0
	lsrs	r3, r3, #2
.LVL421:
	str	r3, [sp, #8]
.LVL422:
.L495:
	mov	r8, #0
.LVL423:
	.loc 3 579 0
	mov	fp, #20
.L505:
	.loc 3 578 0
	mov	r9, #0
	.loc 3 577 0
	mov	r6, r9
.L496:
	.loc 3 578 0 discriminator 1
	ldrh	r1, [r4, #8]
	uxth	r3, r9
.LVL424:
	cmp	r1, r3
	bhi	.L500
	.loc 3 593 0
	cbz	r6, .L483
	.loc 3 597 0
	mov	r9, #0
	.loc 3 596 0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3760]
	bl	FlashProgPages
.LVL425:
.L502:
	.loc 3 597 0 discriminator 1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L504
	.loc 3 604 0
	ldr	r3, [sp, #8]
.LVL426:
	add	r8, r8, r3
.LVL427:
	.loc 3 605 0
	ldr	r3, [sp, #4]
.LVL428:
	.loc 3 604 0
	uxth	r8, r8
.LVL429:
	.loc 3 605 0
	cmp	r3, r8
	bhi	.L505
	mov	r8, #0
.LVL430:
	.loc 3 611 0 discriminator 1
	mov	r9, #20
.L506:
	.loc 3 610 0 discriminator 1
	uxth	r3, r8
.LVL431:
	cmp	r6, r3
	bhi	.L508
	.loc 3 615 0
	ldr	r3, [sp]
.LVL432:
	cmp	r3, #63
	bls	.L509
	.loc 3 615 0 is_stmt 0 discriminator 1
	cbz	r7, .L483
.L509:
	.loc 3 616 0 is_stmt 1
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3760]
	bl	FlashEraseBlocks
.LVL433:
.L483:
	.loc 3 619 0
	mov	r0, r5
.LVL434:
	add	sp, sp, #20
.LCFI71:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL435:
.L489:
.LCFI72:
	.loc 3 540 0
	uxth	r3, r10
	ldr	r0, [r4, #3760]
.LVL436:
	movs	r2, #0
	mul	r1, r9, r3
	.loc 3 541 0
	add	r3, r3, r4
	.loc 3 540 0
	str	r2, [r0, r1]
	.loc 3 541 0
	ldr	r1, [sp]
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL437:
	mov	r8, r0
	.loc 3 543 0
	cbz	r7, .L485
	.loc 3 544 0
	bl	IsBlkInVendorPart
.LVL438:
	cbnz	r0, .L486
.L485:
	.loc 3 548 0
	mov	r0, r8
	bl	FtlBbmIsBadBlock
.LVL439:
	cbnz	r0, .L487
	.loc 3 549 0
	ldr	r1, [r4, #3760]
	lsl	r3, r8, #10
	.loc 3 551 0
	ldr	r0, [fp, #3772]
	.loc 3 549 0
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	.loc 3 550 0
	ldr	r3, [r4, #3768]
	str	r3, [r1, #8]
	.loc 3 551 0
	ldrh	r3, [r4, #88]
	muls	r3, r6, r3
	.loc 3 552 0
	add	r6, r6, #1
	.loc 3 551 0
	it	mi
	addmi	r3, r3, #3
	.loc 3 552 0
	uxth	r6, r6
	.loc 3 551 0
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L486:
	add	r10, r10, #1
	b	.L484
.L487:
	.loc 3 554 0
	adds	r5, r5, #1
.LVL440:
	uxth	r5, r5
.LVL441:
	b	.L486
.L494:
	.loc 3 564 0
	mul	r3, r9, r8
	ldr	r2, [r4, #3760]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L493
	.loc 3 566 0
	ldr	r0, [r1, #4]
	.loc 3 565 0
	adds	r5, r5, #1
.LVL442:
	uxth	r5, r5
.LVL443:
	.loc 3 566 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL444:
.L493:
	add	r8, r8, #1
	b	.L492
.L511:
	.loc 3 533 0
	movs	r3, #6
.LVL445:
	.loc 3 534 0
	mov	r10, r7
.LVL446:
	.loc 3 533 0
	str	r3, [sp, #8]
	.loc 3 531 0
	movs	r3, #1
.LVL447:
	str	r3, [sp, #4]
	b	.L495
.LVL448:
.L500:
	.loc 3 579 0
	uxth	r3, r9
	ldr	r0, [r4, #3760]
	movs	r2, #0
	mul	r1, fp, r3
	.loc 3 580 0
	add	r3, r3, r4
	.loc 3 579 0
	str	r2, [r0, r1]
	.loc 3 580 0
	ldr	r1, [sp]
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL449:
	str	r0, [sp, #12]
	.loc 3 581 0
	cbz	r7, .L497
	.loc 3 582 0
	bl	IsBlkInVendorPart
.LVL450:
	cbnz	r0, .L498
.L497:
	.loc 3 585 0
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
.LVL451:
	cbnz	r0, .L498
	.loc 3 586 0
	ldr	r1, [r4, #3760]
	ldr	r3, [sp, #12]
	.loc 3 588 0
	ldr	r2, .L528
	.loc 3 586 0
	mla	r1, fp, r6, r1
	.loc 3 588 0
	ldr	r0, [r2, #3768]
	.loc 3 586 0
	add	r3, r8, r3, lsl #10
	str	r3, [r1, #4]
	.loc 3 587 0
	ldr	r3, [r4, #3776]
	str	r3, [r1, #8]
	.loc 3 588 0
	ldrh	r3, [r4, #88]
	muls	r3, r6, r3
	.loc 3 589 0
	add	r6, r6, #1
	.loc 3 588 0
	it	mi
	addmi	r3, r3, #3
	.loc 3 589 0
	uxth	r6, r6
	.loc 3 588 0
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L498:
	add	r9, r9, #1
	b	.L496
.L504:
	.loc 3 598 0
	mul	r3, fp, r9
	ldr	r2, [r4, #3760]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L503
	.loc 3 600 0
	ldr	r0, [r1, #4]
	.loc 3 599 0
	adds	r5, r5, #1
.LVL452:
	uxth	r5, r5
.LVL453:
	.loc 3 600 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL454:
.L503:
	add	r9, r9, #1
	b	.L502
.LVL455:
.L508:
	.loc 3 611 0
	cbz	r7, .L507
	.loc 3 611 0 is_stmt 0 discriminator 1
	mul	r3, r9, r8
	ldr	r2, [r4, #3760]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L507
	.loc 3 612 0 is_stmt 1
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.LVL456:
.L507:
	add	r8, r8, #1
	b	.L506
.L529:
	.align	2
.L528:
	.word	.LANCHOR0
.LFE159:
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	1
	.global	FlashTestBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
.LFB275:
	.loc 1 156 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL457:
	.loc 1 161 0
	cmp	r0, #11
	.loc 1 156 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI73:
	mov	r4, r0
	.pad #92
	sub	sp, sp, #92
.LCFI74:
	.loc 1 161 0
	bls	.L532
.LBB313:
.LBB314:
	.loc 1 165 0
	ldr	r5, .L533
	.loc 1 166 0
	add	r0, sp, #24
.LBB315:
.LBB316:
.LBB317:
	.loc 1 202 0
	movs	r2, #32
	movs	r1, #165
.LBE317:
.LBE316:
.LBE315:
	.loc 1 169 0
	lsls	r4, r4, #10
.LVL458:
	.loc 1 166 0
	str	r0, [sp, #16]
	.loc 1 165 0
	str	r5, [sp, #12]
.LBB320:
.LBB319:
.LBB318:
	.loc 1 202 0
	bl	memset
.LVL459:
.LBE318:
.LBE319:
.LBE320:
	.loc 1 170 0
	movs	r2, #1
.LBB321:
.LBB322:
.LBB323:
	.loc 1 202 0
	mov	r3, #1515870810
.LBE323:
.LBE322:
.LBE321:
	.loc 1 170 0
	mov	r1, r2
	add	r0, sp, #4
.LBB326:
.LBB325:
.LBB324:
	.loc 1 202 0
	str	r3, [r5]
	str	r3, [r5, #4]
.LBE324:
.LBE325:
.LBE326:
	.loc 1 169 0
	str	r4, [sp, #8]
	.loc 1 170 0
	bl	FlashEraseBlocks
.LVL460:
	.loc 1 171 0
	movs	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL461:
	.loc 1 172 0
	ldr	r4, [sp, #4]
	.loc 1 178 0
	movs	r2, #1
	movs	r1, #0
	add	r0, sp, #4
	.loc 1 172 0
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
	.loc 1 178 0
	bl	FlashEraseBlocks
.LVL462:
.L530:
.LBE314:
.LBE313:
	.loc 1 180 0
	mov	r0, r4
.LVL463:
	add	sp, sp, #92
.LCFI75:
	@ sp needed
	pop	{r4, r5, pc}
.LVL464:
.L532:
.LCFI76:
	.loc 1 162 0
	movs	r4, #0
.LVL465:
	b	.L530
.L534:
	.align	2
.L533:
	.word	ftl_temp_buf
.LFE275:
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	1
	.global	FlashGetBadBlockList
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
.LFB276:
	.loc 1 183 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL466:
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI77:
.LBB337:
.LBB338:
.LBB339:
	.loc 1 202 0
	mov	r2, #256
.LBE339:
.LBE338:
.LBE337:
	.loc 1 183 0
	mov	r5, r1
.LBB344:
.LBB342:
.LBB340:
	.loc 1 202 0
	movs	r1, #255
.LVL467:
.LBE340:
.LBE342:
.LBE344:
	.loc 1 183 0
	mov	r4, r0
.LBB345:
.LBB343:
.LBB341:
	.loc 1 202 0
	bl	memset
.LVL468:
.LBE341:
.LBE343:
.LBE345:
	.loc 1 187 0
	ldr	r3, .L542
	mov	r1, r5
	mov	r0, r4
	ldr	r3, [r3]
	blx	r3
.LVL469:
	uxth	r0, r0
.LVL470:
	.loc 1 188 0
	cmp	r0, #50
	bls	.L536
.LBB346:
.LBB347:
.LBB348:
	.loc 1 202 0
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
.LVL471:
	bl	memset
.LVL472:
.LBE348:
.LBE347:
.LBE346:
	.loc 1 190 0
	movs	r0, #0
.LVL473:
.L536:
	.loc 1 192 0
	ldr	r3, .L542+4
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L541
	add	r1, r4, r0, lsl #1
.LVL474:
	mov	r3, r4
.L538:
	.loc 1 193 0 discriminator 1
	cmp	r3, r1
	bne	.L539
.L541:
.LVL475:
	.loc 1 198 0
	pop	{r3, r4, r5, pc}
.LVL476:
.L539:
	.loc 1 194 0 discriminator 3
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L538
.L543:
	.align	2
.L542:
	.word	g_nand_ops
	.word	g_nand_phy_info
.LFE276:
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	1
	.global	ftl_memset
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_memset, %function
ftl_memset:
	.fnstart
.LFB277:
	.loc 1 201 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL477:
	push	{r4, lr}
	.save {r4, lr}
.LCFI78:
	.loc 1 201 0
	mov	r4, r0
.LBB353:
	.loc 1 202 0
	cbz	r2, .L545
.LBE353:
.LBB354:
.LBB355:
	bl	memset
.LVL478:
.L545:
.LBE355:
.LBE354:
	.loc 1 203 0 discriminator 8
	mov	r0, r4
	pop	{r4, pc}
.LFE277:
	.fnend
	.size	ftl_memset, .-ftl_memset
	.align	1
	.global	FtlMemInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
.LFB156:
	.loc 3 279 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI79:
	.loc 3 297 0
	movw	r3, #65535
	.loc 3 282 0
	ldr	r4, .L650
	movs	r5, #0
	.loc 3 311 0
	movs	r6, #12
	.loc 3 312 0
	movs	r7, #20
	.loc 3 310 0
	ldrh	r0, [r4, #82]
	.loc 3 297 0
	str	r3, [r4, #3784]
	.loc 3 301 0
	strh	r3, [r4, #3182]	@ movhi
	.loc 3 302 0
	strh	r3, [r4, #3184]	@ movhi
	.loc 3 303 0
	movs	r3, #32
	strh	r3, [r4, #3128]	@ movhi
	.loc 3 310 0
	lsls	r0, r0, #1
	.loc 3 304 0
	movs	r3, #128
	.loc 3 282 0
	str	r5, [r4, #2472]
	.loc 3 304 0
	strh	r3, [r4, #3130]	@ movhi
	.loc 3 283 0
	str	r5, [r4, #2476]
	.loc 3 284 0
	str	r5, [r4, #2460]
	.loc 3 285 0
	str	r5, [r4, #2448]
	.loc 3 286 0
	str	r5, [r4, #2444]
	.loc 3 287 0
	str	r5, [r4, #2452]
	.loc 3 288 0
	str	r5, [r4, #2456]
	.loc 3 289 0
	str	r5, [r4, #2440]
	.loc 3 290 0
	str	r5, [r4, #2480]
	.loc 3 291 0
	str	r5, [r4, #2484]
	.loc 3 292 0
	str	r5, [r4, #2492]
	.loc 3 293 0
	str	r5, [r4, #2496]
	.loc 3 294 0
	str	r5, [r4, #2500]
	.loc 3 295 0
	str	r5, [r4, #3780]
	.loc 3 296 0
	str	r5, [r4, #3140]
	.loc 3 298 0
	str	r5, [r4, #3788]
	.loc 3 299 0
	str	r5, [r4, #3132]
	.loc 3 300 0
	str	r5, [r4, #3764]
	.loc 3 305 0
	strh	r5, [r4, #3136]	@ movhi
	.loc 3 306 0
	strh	r5, [r4, #3186]	@ movhi
	.loc 3 307 0
	strh	r5, [r4, #3222]	@ movhi
	.loc 3 310 0
	bl	ftl_malloc
.LVL479:
	str	r0, [r4, #3168]
	.loc 3 311 0
	ldrh	r0, [r4, #82]
	muls	r0, r6, r0
	bl	ftl_malloc
.LVL480:
	.loc 3 312 0
	ldrh	r3, [r4, #8]
	.loc 3 311 0
	str	r0, [r4, #3176]
	.loc 3 312 0
	muls	r7, r3, r7
	.loc 3 313 0
	lsl	r8, r7, #2
	mov	r0, r8
	bl	ftl_malloc
.LVL481:
	str	r0, [r4, #3792]
	.loc 3 314 0
	mov	r0, r7
	bl	ftl_malloc
.LVL482:
	str	r0, [r4, #3796]
	.loc 3 315 0
	mov	r0, r8
	bl	ftl_malloc
.LVL483:
	str	r0, [r4, #3800]
	.loc 3 316 0
	mov	r0, r7
	bl	ftl_malloc
.LVL484:
	str	r0, [r4, #3760]
	.loc 3 317 0
	mov	r0, r7
	bl	ftl_malloc
.LVL485:
	.loc 3 320 0
	ldrh	r3, [r4, #8]
	.loc 3 319 0
	ldrh	r7, [r4, #86]
	.loc 3 317 0
	str	r0, [r4, #3160]
	.loc 3 320 0
	lsls	r3, r3, #1
	.loc 3 321 0
	mov	r0, r7
	.loc 3 320 0
	adds	r3, r3, #1
	str	r3, [r4, #3164]
	.loc 3 321 0
	bl	ftl_malloc
.LVL486:
	str	r0, [r4, #2244]
	.loc 3 322 0
	mov	r0, r7
	bl	ftl_malloc
.LVL487:
	str	r0, [r4, #3804]
	.loc 3 323 0
	mov	r0, r7
	bl	ftl_malloc
.LVL488:
	str	r0, [r4, #3808]
	.loc 3 324 0
	ldr	r0, [r4, #3164]
	muls	r0, r7, r0
	bl	ftl_malloc
.LVL489:
	str	r0, [r4, #3152]
	.loc 3 325 0
	mov	r0, r7
	bl	ftl_malloc
.LVL490:
	str	r0, [r4, #3776]
	.loc 3 326 0
	mov	r0, r7
	bl	ftl_malloc
.LVL491:
	str	r0, [r4, #3768]
	.loc 3 327 0
	ldr	r0, [r4, #3164]
	muls	r0, r6, r0
	bl	ftl_malloc
.LVL492:
	.loc 3 329 0
	ldrh	r3, [r4, #88]
	ldrh	r7, [r4, #8]
	.loc 3 327 0
	str	r0, [r4, #3148]
	.loc 3 329 0
	muls	r7, r3, r7
	.loc 3 330 0
	mov	r0, r7
	bl	ftl_malloc
.LVL493:
	str	r0, [r4, #3244]
	.loc 3 331 0
	lsls	r0, r7, #2
	bl	ftl_malloc
.LVL494:
	.loc 3 332 0
	ldrh	r3, [r4, #88]
	.loc 3 331 0
	str	r0, [r4, #3772]
	.loc 3 332 0
	ldr	r0, [r4, #3164]
	muls	r0, r3, r0
	bl	ftl_malloc
.LVL495:
	str	r0, [r4, #3156]
	.loc 3 335 0
	ldrh	r0, [r4, #18]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3812]	@ movhi
	.loc 3 336 0
	bl	ftl_malloc
.LVL496:
	.loc 3 337 0
	ldrh	r3, [r4, #3812]
	.loc 3 336 0
	str	r0, [r4, #3816]
	.loc 3 338 0
	ldr	r0, .L650+4
	.loc 3 337 0
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	.loc 3 338 0
	and	r0, r0, r3, lsl #9
	.loc 3 337 0
	strh	r3, [r4, #3812]	@ movhi
	.loc 3 338 0
	bl	ftl_malloc
.LVL497:
	.loc 3 345 0
	ldrh	r7, [r4, #18]
	.loc 3 338 0
	str	r0, [r4, #3820]
	.loc 3 339 0
	adds	r0, r0, #32
	str	r0, [r4, #2248]
	.loc 3 345 0
	lsls	r7, r7, #1
	.loc 3 347 0
	mov	r0, r7
	bl	ftl_malloc
.LVL498:
	str	r0, [r4, #3824]
	.loc 3 349 0
	mov	r0, r7
	bl	ftl_malloc
.LVL499:
	.loc 3 350 0
	ldr	r7, [r4, #104]
	.loc 3 349 0
	str	r0, [r4, #2260]
	.loc 3 350 0
	lsls	r7, r7, #1
	.loc 3 351 0
	mov	r0, r7
	bl	ftl_malloc
.LVL500:
	str	r0, [r4, #3828]
	.loc 3 352 0
	mov	r0, r7
	bl	ftl_malloc
.LVL501:
	str	r0, [r4, #3832]
	.loc 3 353 0
	ldrh	r0, [r4, #18]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
.LVL502:
	str	r0, [r4]
	.loc 3 354 0
	ldrh	r0, [r4, #96]
	lsls	r0, r0, #1
	bl	ftl_malloc
.LVL503:
	str	r0, [r4, #124]
	.loc 3 355 0
	ldrh	r0, [r4, #96]
	lsls	r0, r0, #1
	bl	ftl_malloc
.LVL504:
	str	r0, [r4, #3836]
	.loc 3 356 0
	ldrh	r0, [r4, #96]
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL505:
	str	r0, [r4, #3840]
	.loc 3 357 0
	ldrh	r0, [r4, #98]
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL506:
	.loc 3 358 0
	ldrh	r2, [r4, #98]
	mov	r1, r5
	.loc 3 357 0
	str	r0, [r4, #3844]
	add	r5, r4, #156
	.loc 3 358 0
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL507:
	.loc 3 360 0
	ldrh	r0, [r4, #112]
	.loc 3 361 0
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL508:
	str	r0, [r4, #3848]
	.loc 3 362 0
	ldr	r0, [r4, #104]
	.loc 3 363 0
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL509:
	str	r0, [r4, #3852]
	.loc 3 364 0
	ldrh	r0, [r4, #114]
	.loc 3 365 0
	muls	r0, r6, r0
	bl	ftl_malloc
.LVL510:
	.loc 3 366 0
	ldrh	r3, [r4, #114]
	.loc 3 365 0
	str	r0, [r4, #2424]
	.loc 3 366 0
	ldrh	r0, [r4, #86]
	.loc 3 367 0
	muls	r0, r3, r0
	bl	ftl_malloc
.LVL511:
	.loc 3 368 0
	ldrh	r3, [r4, #18]
	.loc 3 367 0
	str	r0, [r4, #3856]
	.loc 3 369 0
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
.LVL512:
	str	r0, [r4, #2252]
	.loc 3 370 0
	ldrh	r0, [r4, #74]
	.loc 3 371 0
	ldrh	r3, [r4, #30]
	.loc 3 370 0
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3860]	@ movhi
	.loc 3 371 0
	muls	r0, r3, r0
	.loc 3 372 0
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL513:
	.loc 3 374 0
	ldrh	r2, [r4, #3860]
	.loc 3 373 0
	movs	r3, #1
.LVL514:
	ldrh	r6, [r4, #30]
	.loc 3 372 0
	str	r0, [r4, #156]
	lsls	r2, r2, #2
	.loc 3 373 0
	mov	r1, r2
.L550:
	.loc 3 373 0 is_stmt 0 discriminator 1
	cmp	r3, r6
	bcc	.L551
	ldr	r2, .L650+8
	.loc 3 379 0 is_stmt 1
	movs	r1, #0
	add	r3, r2, r3, lsl #2
.LVL515:
	adds	r2, r2, #56
	adds	r3, r3, #24
.L552:
	.loc 3 377 0 discriminator 1
	cmp	r2, r3
	bne	.L553
	.loc 3 383 0
	ldr	r3, [r4, #3828]
	cbnz	r3, .L554
.L556:
	.loc 3 385 0
	ldr	r1, .L650+12
	ldr	r0, .L650+16
	bl	printk
.LVL516:
	.loc 3 386 0
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.LVL517:
.L551:
	.loc 3 374 0 discriminator 3
	ldr	r0, [r4, #156]
	.loc 3 373 0 discriminator 3
	adds	r3, r3, #1
	.loc 3 374 0 discriminator 3
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L550
.LVL518:
.L553:
	.loc 3 379 0 discriminator 2
	str	r1, [r3, #4]!
	b	.L552
.L651:
	.align	2
.L650:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+128
	.word	.LANCHOR1+186
	.word	.LC78
.L554:
	.loc 3 388 0
	ldr	r3, [r4, #3832]
	cmp	r3, #0
	beq	.L556
	.loc 3 393 0
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L556
	.loc 3 398 0
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L556
	.loc 3 404 0
	ldr	r3, [r4, #2424]
	cmp	r3, #0
	beq	.L556
	.loc 3 409 0
	ldr	r3, [r4, #3856]
	cmp	r3, #0
	beq	.L556
	.loc 3 414 0
	ldr	r3, [r4, #2252]
	cmp	r3, #0
	beq	.L556
	.loc 3 419 0
	ldr	r3, [r4, #156]
	cmp	r3, #0
	beq	.L556
	.loc 3 425 0
	ldr	r3, [r4, #2260]
	cmp	r3, #0
	beq	.L556
	.loc 3 430 0
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L556
	.loc 3 430 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L556
	.loc 3 435 0 is_stmt 1
	ldr	r3, [r4, #3792]
	cmp	r3, #0
	beq	.L556
	.loc 3 435 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3800]
	cmp	r3, #0
	beq	.L556
	.loc 3 435 0 discriminator 2
	ldr	r3, [r4, #3760]
	cmp	r3, #0
	beq	.L556
	.loc 3 435 0 discriminator 3
	ldr	r3, [r4, #3160]
	cmp	r3, #0
	beq	.L556
	.loc 3 435 0 discriminator 4
	ldr	r3, [r4, #3796]
	cmp	r3, #0
	beq	.L556
	.loc 3 440 0 is_stmt 1
	ldr	r3, [r4, #2244]
	cmp	r3, #0
	beq	.L556
	.loc 3 440 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L556
	.loc 3 440 0 discriminator 2
	ldr	r3, [r4, #3808]
	cmp	r3, #0
	beq	.L556
	.loc 3 440 0 discriminator 3
	ldr	r3, [r4, #3152]
	cmp	r3, #0
	beq	.L556
	.loc 3 441 0 is_stmt 1
	ldr	r3, [r4, #3776]
	cmp	r3, #0
	beq	.L556
	.loc 3 441 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3768]
	cmp	r3, #0
	beq	.L556
	.loc 3 441 0 discriminator 2
	ldr	r3, [r4, #3148]
	cmp	r3, #0
	beq	.L556
	.loc 3 446 0 is_stmt 1
	ldr	r3, [r4, #3244]
	cmp	r3, #0
	beq	.L556
	.loc 3 446 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3772]
	cmp	r3, #0
	beq	.L556
	.loc 3 446 0 discriminator 2
	ldr	r3, [r4, #3156]
	cmp	r3, #0
	beq	.L556
	.loc 3 452 0 is_stmt 1
	ldr	r3, [r4, #2248]
	cmp	r3, #0
	beq	.L556
	.loc 3 452 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L556
	.loc 3 458 0 is_stmt 1
	ldr	r3, [r4, #124]
	cmp	r3, #0
	beq	.L556
	.loc 3 464 0
	ldr	r3, .L652
	ldr	r2, [r3, #3836]
	cmp	r2, #0
	beq	.L556
	.loc 3 470 0
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L556
	.loc 3 476 0
	ldr	r3, [r3, #3844]
	cmp	r3, #0
	beq	.L556
	.loc 3 483 0
	movs	r0, #0
	.loc 3 484 0
	pop	{r4, r5, r6, r7, r8, pc}
.L653:
	.align	2
.L652:
	.word	.LANCHOR0
.LFE156:
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	1
	.global	FtlBbt2Bitmap
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
.LFB175:
	.loc 4 66 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL519:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI80:
	.loc 4 66 0
	mov	r5, r0
	.loc 4 69 0
	ldr	r6, .L660
	.loc 4 66 0
	mov	r7, r1
	subs	r4, r5, #2
	.loc 4 72 0
	movw	r8, #65535
	.loc 4 74 0
	ldr	r9, .L660+4
	addw	r5, r5, #1022
	.loc 4 69 0
	ldrh	r2, [r6, #3860]
	movs	r1, #0
.LVL520:
	.loc 4 74 0
	ldr	r10, .L660+8
	.loc 4 69 0
	mov	r0, r7
.LVL521:
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL522:
.L657:
	.loc 4 72 0
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L654
	.loc 4 74 0
	ldrh	r2, [r6, #74]
	cmp	r2, r3
	bhi	.L656
	.loc 4 74 0 is_stmt 0 discriminator 1
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	printk
.LVL523:
.L656:
	.loc 4 75 0 is_stmt 1 discriminator 2
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	.loc 4 70 0 discriminator 2
	cmp	r5, r4
	.loc 4 75 0 discriminator 2
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	.loc 4 70 0 discriminator 2
	bne	.L657
.L654:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL524:
.L661:
	.align	2
.L660:
	.word	.LANCHOR0
	.word	.LANCHOR1+197
	.word	.LC1
.LFE175:
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	1
	.global	load_l2p_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
.LFB203:
	.loc 2 442 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL525:
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI81:
	.loc 2 442 0
	mov	r6, r0
	.loc 2 446 0
	ldr	r4, .L668
	.loc 2 442 0
	mov	r8, r1
	.loc 2 446 0
	ldrh	r3, [r4, #112]
	cmp	r3, r0
	bcs	.L663
	.loc 2 446 0 is_stmt 0 discriminator 1
	mov	r2, #446
	ldr	r1, .L668+4
	ldr	r0, .L668+8
	bl	printk
.LVL526:
.L663:
	.loc 2 447 0 is_stmt 1
	ldr	r3, [r4, #3848]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
.LVL527:
	.loc 2 449 0
	cbnz	r7, .L664
	.loc 2 450 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2424]
	ldrh	r2, [r4, #86]
	movs	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL528:
	.loc 2 451 0
	ldr	r3, [r4, #2424]
	strh	r6, [r3, r5]	@ movhi
	.loc 2 452 0
	ldr	r3, [r4, #2424]
	add	r5, r5, r3
	str	r7, [r5, #4]
.LVL529:
.L665:
	.loc 2 473 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.LVL530:
.L664:
	.loc 2 456 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2424]
	.loc 2 458 0
	movs	r2, #1
	ldr	r0, .L668+12
	mov	r1, r2
	.loc 2 455 0
	str	r7, [r4, #3228]
	.loc 2 456 0
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3232]
	.loc 2 457 0
	ldr	r3, [r4, #3244]
	str	r3, [r4, #3236]
	.loc 2 458 0
	bl	FlashReadPages
.LVL531:
	.loc 2 459 0
	ldr	r8, [r4, #3236]
.LVL532:
	.loc 2 461 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L666
	.loc 2 462 0
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L668+16
	bl	printk
.LVL533:
	.loc 2 463 0
	movs	r3, #4
	ldr	r1, [r4, #3236]
	mov	r2, r3
	ldr	r0, .L668+20
	bl	rknand_print_hex
.LVL534:
	.loc 2 464 0
	ldrh	r3, [r4, #112]
	movs	r2, #4
	ldr	r1, [r4, #3848]
	ldr	r0, .L668+24
	bl	rknand_print_hex
.LVL535:
.L666:
	.loc 2 467 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L667
	.loc 2 467 0 is_stmt 0 discriminator 1
	movw	r2, #467
	ldr	r1, .L668+4
	ldr	r0, .L668+8
	bl	printk
.LVL536:
.L667:
	.loc 2 470 0 is_stmt 1
	ldr	r3, [r4, #2424]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	.loc 2 471 0
	strh	r6, [r3, r5]	@ movhi
	.loc 2 472 0
	b	.L665
.L669:
	.align	2
.L668:
	.word	.LANCHOR0
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0+3224
	.word	.LC79
	.word	.LC80
	.word	.LC81
.LFE203:
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	1
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
.LFB204:
	.loc 2 476 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL537:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI82:
.LVL538:
	.loc 2 476 0
	mov	r4, r0
	.loc 2 485 0
	ldrh	r2, [r0, #10]
	movs	r1, #0
	.loc 2 479 0
	ldr	r5, [r0, #20]
.LVL539:
	.loc 2 477 0
	ldr	r9, [r0, #12]
.LVL540:
	.loc 2 478 0
	ldr	r6, [r0, #24]
.LVL541:
	.loc 2 485 0
	lsls	r2, r2, #1
	mov	r0, r5
.LVL542:
	bl	ftl_memset
.LVL543:
	.loc 2 486 0
	movs	r3, #0
.LVL544:
.L671:
	.loc 2 486 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #6]
.LVL545:
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L675
	.loc 2 494 0 is_stmt 1
	ldrh	fp, [r5]
.LVL546:
	.loc 2 497 0
	movs	r6, #0
.LVL547:
	.loc 2 498 0
	ldr	r2, .L684
	.loc 2 495 0
	mov	r10, r6
.LVL548:
.L676:
	.loc 2 497 0 discriminator 1
	ldrh	r1, [r4, #10]
	uxth	r3, r6
	cmp	r1, r3
	bhi	.L680
	.loc 2 513 0
	mov	r0, r10
	add	sp, sp, #12
.LCFI83:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL549:
.L675:
.LCFI84:
	.loc 2 487 0
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	.loc 2 488 0
	movs	r2, #0
	.loc 2 487 0
	ubfx	r1, r1, #10, #16
.LVL550:
.L672:
	.loc 2 488 0 discriminator 1
	ldrh	r7, [r4, #10]
	uxth	r0, r2
	cmp	r7, r0
	bhi	.L674
	adds	r3, r3, #1
	b	.L671
.L674:
	.loc 2 489 0
	uxth	r0, r2
	adds	r2, r2, #1
	ldrh	r7, [r9, r0, lsl #1]
	cmp	r7, r1
	.loc 2 490 0
	ittt	eq
	ldrheq	r7, [r5, r0, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r5, r0, lsl #1]	@ movhi
	b	.L672
.LVL551:
.L680:
	.loc 2 498 0
	ldrh	r1, [r4]
	uxth	r7, r6
	cmp	r1, r3
	bne	.L677
	.loc 2 498 0 is_stmt 0 discriminator 1
	ldrh	r1, [r2, #80]
	ldrh	r0, [r4, #2]
	cmp	r0, r1
	.loc 2 499 0 is_stmt 1 discriminator 1
	it	cc
	strhcc	r1, [r5, r7, lsl #1]	@ movhi
.L677:
	.loc 2 500 0
	ldrh	r8, [r5, r7, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r3
	movhi	fp, r8
	.loc 2 504 0
	cmp	r8, #0
	bne	.L679
	.loc 2 504 0 is_stmt 0 discriminator 1
	ldrh	r0, [r9, r7, lsl #1]
	cbz	r0, .L679
	.loc 2 506 0 is_stmt 1
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
.LVL552:
	.loc 2 507 0
	strh	r8, [r9, r7, lsl #1]	@ movhi
	.loc 2 508 0
	ldr	r2, [sp, #4]
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L679:
	adds	r6, r6, #1
	b	.L676
.L685:
	.align	2
.L684:
	.word	.LANCHOR0
.LFE204:
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	1
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
.LFB207:
	.loc 2 583 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL553:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI85:
	.loc 2 591 0
	movw	r2, #65535
	ldrh	r3, [r0]
	.loc 2 583 0
	mov	r4, r0
	.loc 2 586 0
	ldr	r5, [r0, #12]
.LVL554:
	.loc 2 591 0
	cmp	r3, r2
	bne	.L687
	.loc 2 592 0
	ldrh	r3, [r0, #8]
	cbz	r3, .L688
	.loc 2 592 0 is_stmt 0 discriminator 1
	mov	r2, #592
	ldr	r1, .L696
	ldr	r0, .L696+4
	bl	printk
.LVL555:
.L688:
	.loc 2 593 0 is_stmt 1
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 594 0
	bl	FtlFreeSysBlkQueueOut
.LVL556:
	.loc 2 595 0
	movs	r3, #0
	.loc 2 594 0
	strh	r0, [r5]	@ movhi
	.loc 2 595 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 596 0
	strh	r3, [r4]	@ movhi
	.loc 2 597 0
	ldr	r3, [r4, #28]
	adds	r3, r3, #1
	str	r3, [r4, #28]
.LVL557:
.L689:
	.loc 2 628 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL558:
.L687:
	.loc 2 601 0
	ldrh	r6, [r5, r3, lsl #1]
	.loc 2 612 0
	movs	r1, #255
	.loc 2 602 0
	ldrh	r3, [r0, #2]
	ldr	r5, .L696+8
.LVL559:
	.loc 2 607 0
	ldr	r2, [r0, #28]
	.loc 2 587 0
	ldr	r7, [r0, #24]
.LVL560:
	.loc 2 602 0
	orr	r3, r3, r6, lsl #10
	str	r3, [r5, #3228]
	.loc 2 603 0
	ldr	r3, [r5, #2244]
	str	r3, [r5, #3232]
	.loc 2 604 0
	ldr	r3, [r5, #3244]
	str	r3, [r5, #3236]
	.loc 2 607 0
	str	r2, [r3, #4]
	.loc 2 608 0
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	.loc 2 609 0
	ldrh	r2, [r0, #4]
	.loc 2 610 0
	strh	r6, [r3, #2]	@ movhi
	.loc 2 609 0
	strh	r2, [r3]	@ movhi
	.loc 2 612 0
	ldrh	r2, [r5, #80]
	ldr	r0, [r5, #2244]
.LVL561:
	lsls	r2, r2, #3
	bl	ftl_memset
.LVL562:
	.loc 2 614 0
	movs	r2, #0
	.loc 2 613 0
	mov	r3, r2
.LVL563:
.L690:
	.loc 2 614 0 discriminator 1
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L692
	.loc 2 621 0
	movs	r2, #1
	movs	r3, #0
.LVL564:
	mov	r1, r2
	ldr	r0, .L696+12
	bl	FlashProgPages
.LVL565:
	.loc 2 622 0
	ldrh	r3, [r4, #2]
	.loc 2 626 0
	mov	r0, r4
	.loc 2 622 0
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	.loc 2 626 0
	bl	ftl_map_blk_gc
.LVL566:
	.loc 2 627 0
	b	.L689
.LVL567:
.L692:
	uxth	r1, r2
	.loc 2 615 0
	ldr	r0, [r7, r1, lsl #2]
	cmp	r6, r0, lsr #10
	bne	.L691
	.loc 2 617 0
	ldr	r0, [r5, #2244]
	.loc 2 616 0
	adds	r3, r3, #1
.LVL568:
	uxth	r3, r3
.LVL569:
	.loc 2 617 0
	str	r1, [r0, r3, lsl #3]
	.loc 2 618 0
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #2244]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L691:
	adds	r2, r2, #1
	b	.L690
.L697:
	.align	2
.L696:
	.word	.LANCHOR1+227
	.word	.LC1
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
.LFE207:
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	1
	.global	FtlMapWritePage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
.LFB208:
	.loc 2 631 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL570:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI86:
	.loc 2 631 0
	mov	r4, r0
	.loc 2 637 0
	ldr	r7, .L717
	.loc 2 631 0
	mov	r8, r1
	mov	r10, r2
	.loc 2 634 0
	movs	r6, #0
.LVL571:
	.loc 2 650 0
	ldr	r9, .L717+20
	mov	r5, r7
.L699:
	.loc 2 637 0
	ldr	r3, [r7, #2456]
	adds	r3, r3, #1
	str	r3, [r7, #2456]
	.loc 2 638 0
	ldrh	r3, [r7, #80]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L700
	.loc 2 638 0 is_stmt 0 discriminator 1
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L701
.L700:
	.loc 2 639 0 is_stmt 1
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.LVL572:
.L701:
	.loc 2 650 0
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L702
	.loc 2 650 0 is_stmt 0 discriminator 1
	movw	r2, #650
	mov	r1, r9
	ldr	r0, .L717+4
	bl	printk
.LVL573:
.L702:
	.loc 2 651 0 is_stmt 1
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L703
	.loc 2 651 0 is_stmt 0 discriminator 1
	movw	r2, #651
	mov	r1, r9
	ldr	r0, .L717+4
	bl	printk
.LVL574:
.L703:
	.loc 2 652 0 is_stmt 1
	ldrh	r2, [r4]
.LBB361:
.LBB362:
.LBB363:
	.loc 1 202 0
	movs	r1, #16
.LBE363:
.LBE362:
.LBE361:
	.loc 2 652 0
	ldr	r3, [r4, #12]
	.loc 2 656 0
	ldr	r0, [r5, #3244]
	.loc 2 652 0
	ldrh	fp, [r3, r2, lsl #1]
	.loc 2 654 0
	ldrh	r3, [r4, #2]
	.loc 2 656 0
	str	r0, [r5, #3236]
	.loc 2 655 0
	str	r10, [r5, #3232]
	.loc 2 654 0
	orr	r3, r3, fp, lsl #10
	str	r3, [r5, #3228]
.LBB366:
.LBB365:
.LBB364:
	.loc 1 202 0
	bl	__memzero
.LVL575:
.LBE364:
.LBE365:
.LBE366:
	.loc 2 659 0
	ldr	r3, [r5, #3236]
.LVL576:
	.loc 2 660 0
	ldr	r2, [r4, #28]
	.loc 2 664 0
	ldr	r0, .L717+8
	.loc 2 661 0
	strh	r8, [r3, #8]	@ movhi
	.loc 2 660 0
	str	r2, [r3, #4]
	.loc 2 662 0
	ldrh	r2, [r4, #4]
	.loc 2 663 0
	strh	fp, [r3, #2]	@ movhi
	.loc 2 662 0
	strh	r2, [r3]	@ movhi
	.loc 2 664 0
	movs	r3, #1
.LVL577:
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL578:
	.loc 2 665 0
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	.loc 2 667 0
	ldr	r2, [r5, #3224]
	adds	r2, r2, #1
	bne	.L704
	.loc 2 668 0
	ldr	r1, [r5, #3228]
	.loc 2 669 0
	adds	r6, r6, #1
.LVL579:
	.loc 2 668 0
	ldr	r0, .L717+12
	.loc 2 669 0
	uxth	r6, r6
.LVL580:
	.loc 2 668 0
	bl	printk
.LVL581:
	.loc 2 670 0
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	.loc 2 671 0
	ittt	ls
	ldrhls	r3, [r5, #80]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	.loc 2 673 0
	cmp	r6, #3
	bls	.L706
	.loc 2 674 0
	mov	r2, r6
	ldr	r1, [r5, #3228]
	ldr	r0, .L717+16
	bl	printk
.LVL582:
.L707:
	b	.L707
.L706:
	.loc 2 678 0
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L699
.L716:
	b	.L716
.L704:
	.loc 2 682 0
	cmp	r3, #1
	beq	.L699
	.loc 2 685 0
	ldr	r2, [r5, #3228]
	.loc 2 687 0
	movs	r0, #0
	.loc 2 685 0
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	.loc 2 687 0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL583:
.L718:
	.align	2
.L717:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+3224
	.word	.LC82
	.word	.LC83
	.word	.LANCHOR1+258
.LFE208:
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	1
	.global	ftl_map_blk_gc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
.LFB206:
	.loc 2 537 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL584:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI87:
.LVL585:
	.loc 2 537 0
	mov	r4, r0
	.loc 2 541 0
	ldr	r6, [r0, #12]
.LVL586:
	.loc 2 542 0
	ldr	r9, [r0, #24]
.LVL587:
	.loc 2 545 0
	bl	ftl_free_no_use_map_blk
.LVL588:
	.loc 2 547 0
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L731
	subs	r3, r3, #4
	cmp	r2, r3
	blt	.L720
	.loc 2 548 0
	uxth	r0, r0
	ldrh	r8, [r6, r0, lsl #1]
.LVL589:
	.loc 2 549 0
	cmp	r8, #0
	beq	.L720
	.loc 2 549 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #32]
	cbnz	r3, .L720
	.loc 2 550 0 is_stmt 1
	movs	r2, #1
	str	r2, [r4, #32]
	.loc 2 551 0
	strh	r3, [r6, r0, lsl #1]	@ movhi
	.loc 2 552 0
	ldrh	r3, [r4, #8]
	.loc 2 553 0
	ldrh	r2, [r4, #2]
	.loc 2 552 0
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 553 0
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L721
	.loc 2 554 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL590:
.L721:
	.loc 2 564 0 discriminator 1
	ldr	fp, .L731+12
	.loc 2 537 0 discriminator 1
	movs	r6, #0
.LVL591:
.L722:
	.loc 2 556 0 discriminator 1
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L726
	.loc 2 572 0
	movs	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.LVL592:
	.loc 2 573 0
	movs	r3, #0
	str	r3, [r4, #32]
.LVL593:
.L720:
	.loc 2 577 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L727
	.loc 2 578 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL594:
.L727:
	.loc 2 580 0
	movs	r0, #0
	add	sp, sp, #12
.LCFI88:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL595:
.L726:
.LCFI89:
	uxth	r7, r6
	.loc 2 557 0
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L723
	.loc 2 558 0
	ldr	r3, [r5, #3804]
	.loc 2 562 0
	ldr	r0, .L731+4
	.loc 2 558 0
	str	r3, [r5, #3232]
	.loc 2 559 0
	ldr	r3, [r5, #3244]
	str	r3, [r5, #3236]
	.loc 2 560 0
	ldr	r2, [r9, r7, lsl #2]
	.loc 2 559 0
	str	r3, [sp, #4]
	.loc 2 560 0
	str	r2, [r5, #3228]
	.loc 2 562 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL596:
	.loc 2 564 0
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #8]
	cmp	r3, r10
	beq	.L724
	.loc 2 564 0 is_stmt 0 discriminator 1
	mov	r2, #564
	ldr	r1, .L731+8
	mov	r0, fp
	bl	printk
.LVL597:
.L724:
	.loc 2 565 0 is_stmt 1
	ldr	r3, [r5, #3224]
	adds	r3, r3, #1
	bne	.L725
	.loc 2 566 0
	movs	r3, #0
	str	r3, [r9, r7, lsl #2]
.L723:
	adds	r6, r6, #1
	b	.L722
.L725:
	.loc 2 568 0
	ldr	r2, [r5, #3232]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.LVL598:
	b	.L723
.L732:
	.align	2
.L731:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
	.word	.LANCHOR1+274
	.word	.LC1
.LFE206:
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	1
	.global	flush_l2p_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
.LFB209:
	.loc 2 690 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL599:
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI90:
	.loc 2 693 0
	movs	r4, #12
	ldr	r5, .L734
	muls	r4, r0, r4
	ldr	r3, [r5, #2424]
	.loc 2 694 0
	addw	r0, r5, #3864
.LVL600:
	.loc 2 693 0
	adds	r2, r3, r4
	.loc 2 694 0
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
.LVL601:
	.loc 2 695 0
	ldr	r3, [r5, #2424]
	.loc 2 697 0
	movs	r0, #0
	.loc 2 695 0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	.loc 2 697 0
	pop	{r3, r4, r5, pc}
.L735:
	.align	2
.L734:
	.word	.LANCHOR0
.LFE209:
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	1
	.global	log2phys
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
.LFB212:
	.loc 2 753 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL602:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI91:
	.loc 2 753 0
	mov	r7, r1
	.loc 2 756 0
	ldr	r4, .L750
	.loc 2 753 0
	mov	r9, r2
	.loc 2 756 0
	ldrh	r5, [r4, #84]
	adds	r3, r5, #7
	.loc 2 757 0
	movs	r5, #1
	.loc 2 756 0
	lsr	r8, r0, r3
	.loc 2 757 0
	lsls	r5, r5, r3
	.loc 2 759 0
	ldr	r3, [r4, #2436]
	.loc 2 757 0
	subs	r5, r5, #1
	.loc 2 756 0
	uxth	r8, r8
.LVL603:
	.loc 2 757 0
	ands	r5, r5, r0
	.loc 2 759 0
	cmp	r0, r3
	.loc 2 757 0
	uxth	r5, r5
.LVL604:
	.loc 2 759 0
	bcc	.L737
	.loc 2 759 0 is_stmt 0 discriminator 1
	movw	r2, #759
	ldr	r1, .L750+4
	ldr	r0, .L750+8
.LVL605:
	bl	printk
.LVL606:
.L737:
	.loc 2 761 0 is_stmt 1
	ldrh	r2, [r4, #114]
	movs	r3, #0
	.loc 2 762 0
	ldr	r1, [r4, #2424]
	mov	r10, #12
.LVL607:
.L738:
	uxth	r6, r3
	.loc 2 761 0 discriminator 1
	cmp	r6, r2
	bcc	.L743
	.loc 2 776 0
	bl	select_l2p_ram_region
.LVL608:
	.loc 2 777 0
	mul	r10, r10, r0
	ldr	r3, [r4, #2424]
	.loc 2 776 0
	mov	r6, r0
	.loc 2 777 0
	ldrh	r1, [r3, r10]
	add	r2, r3, r10
	movw	r3, #65535
	cmp	r1, r3
	beq	.L744
	.loc 2 777 0 is_stmt 0 discriminator 1
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L744
	.loc 2 778 0 is_stmt 1
	bl	flush_l2p_region
.LVL609:
.L744:
	.loc 2 781 0
	mov	r1, r6
	mov	r0, r8
	bl	load_l2p_region
.LVL610:
	.loc 2 782 0
	b	.L739
.LVL611:
.L743:
	adds	r3, r3, #1
	.loc 2 762 0
	mla	r0, r10, r3, r1
.LVL612:
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L738
.L739:
	movs	r3, #12
	.loc 2 764 0
	cmp	r9, #0
	bne	.L740
	.loc 2 765 0
	ldr	r2, [r4, #2424]
	mla	r3, r3, r6, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r7]
.L741:
	.loc 2 771 0
	ldr	r2, [r4, #2424]
	movs	r3, #12
	mla	r6, r3, r6, r2
.LVL613:
	ldr	r3, [r6, #4]
	adds	r2, r3, #1
	beq	.L747
	.loc 2 772 0
	adds	r3, r3, #1
	str	r3, [r6, #4]
.L747:
	.loc 2 783 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL614:
.L740:
	.loc 2 767 0
	muls	r3, r6, r3
	ldr	r2, [r4, #2424]
	ldr	r1, [r7]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r5, lsl #2]
	.loc 2 768 0
	ldr	r2, [r4, #2424]
	.loc 2 769 0
	strh	r8, [r4, #2428]	@ movhi
	.loc 2 768 0
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L741
.L751:
	.align	2
.L750:
	.word	.LANCHOR0
	.word	.LANCHOR1+289
	.word	.LC1
.LFE212:
	.fnend
	.size	log2phys, .-log2phys
	.align	1
	.global	FtlReUsePrevPpa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
.LFB230:
	.loc 2 1628 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL615:
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
.LCFI92:
.LVL616:
	.loc 2 1628 0
	mov	r6, r0
	.loc 2 1631 0
	ldr	r5, .L762
.LVL617:
	.loc 2 1629 0
	ubfx	r0, r1, #10, #16
.LVL618:
	.loc 2 1628 0
	str	r1, [sp, #4]
	.loc 2 1629 0
	bl	P2V_block_in_plane
.LVL619:
	.loc 2 1631 0
	ldr	r2, [r5, #2260]
	.loc 2 1629 0
	mov	r7, r0
.LVL620:
	.loc 2 1631 0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L753
	.loc 2 1632 0
	ldr	r4, [r5, #2272]
	cbz	r4, .L754
.LBB367:
	.loc 2 1636 0
	ldr	r1, [r5, #2252]
	.loc 2 1644 0
	mov	ip, #6
	.loc 2 1636 0
	ldr	r2, .L762+4
	.loc 2 1644 0
	movw	lr, #65535
	.loc 2 1635 0
	ldrh	r0, [r5, #2276]
.LVL621:
	.loc 2 1636 0
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.LVL622:
.L755:
	.loc 2 1637 0 discriminator 1
	uxth	r2, r3
	cmp	r0, r2
	bls	.L754
	.loc 2 1638 0
	cmp	r4, r7
	bne	.L756
	.loc 2 1639 0
	mov	r1, r4
	ldr	r0, .L762+8
.LVL623:
	bl	List_remove_node
.LVL624:
	ldrh	r3, [r5, #2276]
	cbnz	r3, .L757
	.loc 2 1639 0 is_stmt 0 discriminator 1
	movw	r2, #1639
	ldr	r1, .L762+12
	ldr	r0, .L762+16
	bl	printk
.LVL625:
.L757:
	.loc 2 1639 0 discriminator 3
	ldrh	r3, [r5, #2276]
	.loc 2 1640 0 is_stmt 1 discriminator 3
	mov	r0, r4
	.loc 2 1639 0 discriminator 3
	subs	r3, r3, #1
	strh	r3, [r5, #2276]	@ movhi
	.loc 2 1640 0 discriminator 3
	bl	INSERT_DATA_LIST
.LVL626:
	.loc 2 1641 0 discriminator 3
	ldr	r2, [r5, #2260]
	ldrh	r3, [r2, r7, lsl #1]
.L753:
.LBE367:
	.loc 2 1650 0
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L754
.LVL627:
.L756:
.LBB368:
	.loc 2 1644 0
	mul	r4, ip, r4
.LVL628:
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
.LVL629:
	cmp	r4, lr
	bne	.L755
.L754:
.LBE368:
	.loc 2 1652 0
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
.LVL630:
	bl	log2phys
.LVL631:
	.loc 2 1653 0
	add	sp, sp, #12
.LCFI93:
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.LVL632:
.L763:
	.align	2
.L762:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2272
	.word	.LANCHOR1+298
	.word	.LC1
.LFE230:
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	1
	.global	ftl_scan_all_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
.LFB241:
	.loc 2 2275 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI94:
	.loc 2 2281 0
	movs	r5, #0
.LVL633:
	ldr	r6, .L778
	.loc 2 2275 0
	.pad #32
	sub	sp, sp, #32
.LCFI95:
	.loc 2 2280 0
	movs	r1, #0
	.loc 2 2284 0
	ldr	r8, .L778+16
	mov	r4, r6
	.loc 2 2280 0
	ldr	r0, .L778+4
	bl	printk
.LVL634:
.L765:
	.loc 2 2281 0 discriminator 1
	ldr	r3, [r6, #2436]
	cmp	r5, r3
	bcc	.L771
	.loc 2 2303 0
	add	sp, sp, #32
.LCFI96:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL635:
.L771:
.LCFI97:
	.loc 2 2282 0
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
.LVL636:
	.loc 2 2283 0
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L766
	.loc 2 2284 0
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	printk
.LVL637:
.L766:
	.loc 2 2285 0
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L768
	.loc 2 2286 0
	str	r3, [r4, #3228]
	.loc 2 2290 0
	movs	r2, #0
	.loc 2 2288 0
	ldr	r3, [r4, #2244]
	.loc 2 2292 0
	movs	r1, #1
	.loc 2 2289 0
	ldr	r7, [r4, #3244]
	.loc 2 2292 0
	ldr	r0, .L778+8
	.loc 2 2288 0
	str	r3, [r4, #3232]
	.loc 2 2287 0
	str	r5, [r4, #3240]
	.loc 2 2289 0
	str	r7, [r4, #3236]
	.loc 2 2290 0
	str	r2, [r4, #3224]
	.loc 2 2292 0
	bl	FlashReadPages
.LVL638:
	.loc 2 2293 0
	ldr	r3, [r4, #3224]
	cmp	r3, #256
	beq	.L769
	.loc 2 2293 0 is_stmt 0 discriminator 1
	adds	r3, r3, #1
	beq	.L769
	.loc 2 2294 0 is_stmt 1
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L768
.L769:
	.loc 2 2297 0
	ldr	r2, [r4, #3232]
	ldr	r3, [r4, #3236]
	ldr	r0, .L778+12
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	mov	r1, r5
	ldr	r2, [r2]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r3, [r3]
	ldr	r2, [r4, #3228]
	bl	printk
.LVL639:
.L768:
	.loc 2 2281 0 discriminator 2
	adds	r5, r5, #1
	b	.L765
.L779:
	.align	2
.L778:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LANCHOR0+3224
	.word	.LC86
	.word	.LC85
.LFE241:
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	1
	.global	FtlReadRefresh
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
.LFB268:
	.loc 5 419 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	.loc 5 425 0
	ldr	r3, .L793
	.loc 5 419 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI98:
	.pad #88
	sub	sp, sp, #88
.LCFI99:
	mov	r4, r3
	.loc 5 425 0
	ldr	r0, [r3, #2696]
	cmp	r0, #0
	beq	.L781
	.loc 5 426 0
	ldr	r1, [r3, #2700]
	ldr	r2, [r3, #2436]
	cmp	r1, r2
	bcs	.L782
	mov	r5, #2048
.LVL640:
.L787:
.LBB374:
.LBB375:
	.loc 5 428 0
	ldr	r0, [r4, #2700]
	ldr	r3, [r4, #2436]
	cmp	r0, r3
	bcs	.L784
	.loc 5 430 0
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
.LVL641:
	.loc 5 432 0
	ldr	r2, [sp]
	.loc 5 431 0
	ldr	r3, [r4, #2700]
	.loc 5 432 0
	adds	r1, r2, #1
	.loc 5 431 0
	add	r3, r3, #1
	str	r3, [r4, #2700]
	.loc 5 432 0
	beq	.L785
.LBB376:
	.loc 5 435 0
	str	r3, [sp, #20]
	.loc 5 438 0
	add	r0, sp, #88
	.loc 5 436 0
	ldr	r3, .L793+4
	.loc 5 439 0
	movs	r1, #1
	.loc 5 434 0
	str	r2, [sp, #8]
	.loc 5 438 0
	movs	r2, #0
	str	r2, [r0, #-84]!
	.loc 5 436 0
	str	r3, [sp, #12]
	.loc 5 437 0
	add	r3, sp, #24
	str	r3, [sp, #16]
	.loc 5 439 0
	bl	FlashReadPages
.LVL642:
	.loc 5 440 0
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L784
	.loc 5 442 0
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL643:
	bl	FtlGcRefreshBlock
.LVL644:
.L784:
.LBE376:
	mov	r0, #-1
.L780:
.LBE375:
.LBE374:
	.loc 5 466 0
	add	sp, sp, #88
.LCFI100:
	@ sp needed
	pop	{r4, r5, r6, pc}
.L785:
.LCFI101:
.LBB378:
.LBB377:
	.loc 5 427 0
	subs	r5, r5, #1
	bne	.L787
	b	.L784
.LVL645:
.L782:
.LBE377:
.LBE378:
	.loc 5 451 0
	ldr	r2, [r3, #2440]
	.loc 5 449 0
	movs	r0, #0
	str	r0, [r3, #2696]
	.loc 5 450 0
	str	r0, [r3, #2700]
	.loc 5 451 0
	str	r2, [r3, #2692]
	b	.L780
.L781:
.LBB379:
	.loc 5 456 0
	ldr	r1, [r3, #2440]
	ldr	r6, [r3, #2692]
	add	r2, r1, #1048576
	cmp	r6, r2
	bhi	.L790
	.loc 5 454 0
	ldr	r2, [r3, #2496]
	lsrs	r5, r2, #10
	mov	r2, #33554432
	asrs	r2, r2, r5
.LVL646:
	.loc 5 457 0
	add	r2, r2, r6
.LVL647:
	cmp	r1, r2
	bhi	.L790
	.loc 5 458 0
	ldrb	r3, [r3, #2532]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L780
.L790:
	.loc 5 460 0
	movs	r3, #1
	.loc 5 461 0
	movs	r0, #0
	.loc 5 460 0
	str	r3, [r4, #2696]
	.loc 5 461 0
	str	r0, [r4, #2700]
	.loc 5 462 0
	str	r1, [r4, #2692]
	b	.L780
.L794:
	.align	2
.L793:
	.word	.LANCHOR0
	.word	ftl_temp_buf
.LBE379:
.LFE268:
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	1
	.global	ftl_check_vpc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	.fnstart
.LFB240:
	.loc 2 2223 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI102:
	.loc 2 2232 0
	movs	r4, #0
.LVL648:
	ldr	r6, .L819
	.loc 2 2238 0
	ldr	r7, .L819+4
	.loc 2 2229 0
	ldr	r1, .L819+8
	mov	r5, r6
	ldr	r0, .L819+12
	bl	printk
.LVL649:
.LBB386:
.LBB387:
.LBB388:
	.loc 1 202 0
	mov	r1, #8192
	ldr	r0, .L819+4
	bl	__memzero
.LVL650:
.L796:
.LBE388:
.LBE387:
.LBE386:
	.loc 2 2232 0 discriminator 1
	ldr	r3, [r6, #2436]
	cmp	r4, r3
	bcc	.L798
	.loc 2 2245 0 discriminator 1
	ldr	r8, .L819+4
	movs	r4, #0
.LVL651:
	.loc 2 2247 0 discriminator 1
	ldr	r10, .L819+24
	mov	r7, r4
.LVL652:
	.loc 2 2245 0 discriminator 1
	movw	r9, #65535
.L799:
	.loc 2 2243 0 discriminator 1
	ldrh	r2, [r5, #16]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L801
	.loc 2 2255 0
	ldr	r4, [r5, #2272]
	cbz	r4, .L802
.LBB389:
	.loc 2 2258 0
	ldr	r3, [r5, #2252]
	.loc 2 2259 0
	movs	r6, #0
	.loc 2 2257 0
	ldrh	r8, [r5, #2276]
.LVL653:
	.loc 2 2266 0
	mov	fp, #6
	.loc 2 2263 0
	ldr	r9, .L819+4
	.loc 2 2258 0
	subs	r4, r4, r3
	ldr	r3, .L819+16
	asrs	r4, r4, #1
	.loc 2 2263 0
	ldr	r10, .L819+28
	.loc 2 2258 0
	muls	r4, r3, r4
	uxth	r4, r4
.L803:
	.loc 2 2259 0 discriminator 1
	uxth	r3, r6
	cmp	r8, r3
	bls	.L802
	.loc 2 2261 0
	ldr	r3, [r5, #2260]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L804
	.loc 2 2264 0
	movs	r7, #1
	.loc 2 2263 0
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.LVL654:
.L804:
	.loc 2 2266 0
	mul	r4, fp, r4
	ldr	r3, [r5, #2252]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L803
.L802:
.LBE389:
	.loc 2 2271 0
	cbz	r7, .L795
	.loc 2 2271 0 is_stmt 0 discriminator 1
	movw	r2, #2271
	ldr	r1, .L819+8
	ldr	r0, .L819+20
	bl	printk
.LVL655:
.L795:
	.loc 2 2272 0 is_stmt 1
	add	sp, sp, #12
.LCFI103:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL656:
.L798:
.LCFI104:
	.loc 2 2234 0
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
.LVL657:
	.loc 2 2235 0
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L797
	.loc 2 2237 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL658:
	.loc 2 2238 0
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L797:
	.loc 2 2232 0 discriminator 2
	adds	r4, r4, #1
	b	.L796
.LVL659:
.L801:
	.loc 2 2245 0
	ldr	r3, [r5, #2260]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L800
	.loc 2 2247 0
	mov	r1, r6
	mov	r0, r10
	bl	printk
.LVL660:
	.loc 2 2248 0
	ldr	r3, [r5, #2260]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L800
	.loc 2 2248 0 is_stmt 0 discriminator 1
	ldrh	r2, [r8, r6, lsl #1]
	.loc 2 2250 0 is_stmt 1 discriminator 1
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L800:
	adds	r4, r4, #1
	b	.L799
.L820:
	.align	2
.L819:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+314
	.word	.LC87
	.word	-1431655765
	.word	.LC1
	.word	.LC88
	.word	.LC89
.LFE240:
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	1
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	.fnstart
.LFB217:
	.loc 2 897 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL661:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI105:
	.loc 2 897 0
	mov	r6, r0
	.loc 2 903 0
	ldr	r3, [r0, #36]
	cbz	r3, .L821
	.loc 2 899 0
	ldrh	r5, [r0, #6]
	.loc 2 905 0
	movs	r3, #0
	.loc 2 907 0
	ldr	r4, .L825
	.loc 2 900 0
	ldr	r2, [r0, #24]
.LVL662:
	.loc 2 905 0
	str	r3, [r0, #36]
	.loc 2 899 0
	subs	r5, r5, #1
	.loc 2 907 0
	ldr	r0, [r4, #3804]
	.loc 2 930 0
	uxth	r5, r5
	.loc 2 908 0
	ldr	r1, [r4, #3244]
	.loc 2 907 0
	str	r0, [r4, #3232]
	.loc 2 908 0
	str	r1, [r4, #3236]
	.loc 2 930 0
	ldr	r2, [r2, r5, lsl #2]
.LVL663:
	.loc 2 931 0
	str	r2, [r4, #3228]
	.loc 2 932 0
	cbz	r2, .L823
	.loc 2 934 0
	movs	r2, #1
.LVL664:
	addw	r0, r4, #3224
	mov	r1, r2
	bl	FlashReadPages
.LVL665:
.L824:
	.loc 2 941 0
	ldr	r2, [r4, #3232]
	mov	r1, r5
	mov	r0, r6
	.loc 2 943 0
	pop	{r4, r5, r6, lr}
.LCFI106:
.LVL666:
	.loc 2 941 0
	b	FtlMapWritePage
.LVL667:
.L823:
.LCFI107:
	.loc 2 938 0
	ldrh	r2, [r4, #86]
.LVL668:
	movs	r1, #255
	bl	ftl_memset
.LVL669:
	b	.L824
.LVL670:
.L821:
	pop	{r4, r5, r6, pc}
.L826:
	.align	2
.L825:
	.word	.LANCHOR0
.LFE217:
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	1
	.global	FtlScanSysBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
.LFB220:
	.loc 2 1095 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI108:
	.loc 2 1101 0
	movs	r5, #0
	ldr	r4, .L909
	.loc 2 1095 0
	.pad #20
	sub	sp, sp, #20
.LCFI109:
	.loc 2 1103 0
	mov	r1, r5
	ldr	r2, [r4, #104]
	ldr	r0, [r4, #3852]
	.loc 2 1101 0
	strh	r5, [r4, #3904]	@ movhi
	.loc 2 1102 0
	strh	r5, [r4, #120]	@ movhi
	.loc 2 1103 0
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL671:
	.loc 2 1104 0
	ldr	r2, [r4, #104]
	mov	r1, r5
	ldr	r0, [r4, #3828]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL672:
	.loc 2 1105 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #3840]
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL673:
	.loc 2 1106 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #124]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL674:
	.loc 2 1109 0
	ldrh	r8, [r4, #16]
.LVL675:
.LBB395:
.LBB396:
.LBB397:
	.loc 1 202 0
	mov	r3, #-1
	str	r3, [r4, #2556]
	str	r3, [r4, #2560]
	str	r3, [r4, #2564]
.LVL676:
.L828:
.LBE397:
.LBE396:
.LBE395:
	.loc 2 1109 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r8
	bls	.L870
	.loc 2 1120 0 discriminator 1
	movs	r5, #0
	.loc 2 1113 0 discriminator 1
	ldrh	fp, [r4, #8]
	.loc 2 1118 0 discriminator 1
	ldr	r9, [r4, #3792]
	.loc 2 1120 0 discriminator 1
	mov	r7, r5
	.loc 2 1119 0 discriminator 1
	ldr	r3, [r4, #3152]
	.loc 2 1118 0 discriminator 1
	mov	r10, #20
	.loc 2 1120 0 discriminator 1
	ldr	r2, [r4, #3156]
	ldrh	r6, [r4, #88]
.LVL677:
	b	.L871
.L831:
	str	r3, [sp, #8]
	.loc 2 1115 0
	mov	r1, r8
	ldr	r3, .L909+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL678:
	str	r0, [sp, #4]
	.loc 2 1116 0
	bl	FtlBbmIsBadBlock
.LVL679:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L829
	.loc 2 1118 0
	ldr	r1, [sp, #4]
	mla	r0, r10, r7, r9
	lsls	r1, r1, #10
	.loc 2 1119 0
	str	r3, [r0, #8]
	.loc 2 1118 0
	str	r1, [r0, #4]
	.loc 2 1120 0
	mov	r1, r6
	muls	r1, r7, r1
	.loc 2 1121 0
	add	r7, r7, #1
.LVL680:
	.loc 2 1120 0
	it	mi
	addmi	r1, r1, #3
	.loc 2 1121 0
	uxth	r7, r7
.LVL681:
	.loc 2 1120 0
	bic	r1, r1, #3
	add	r1, r1, r2
	str	r1, [r0, #12]
.L829:
	adds	r5, r5, #1
.L871:
	.loc 2 1113 0 discriminator 1
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L831
	.loc 2 1124 0
	cbnz	r7, .L832
.LVL682:
.L869:
	.loc 2 1109 0
	add	r8, r8, #1
.LVL683:
	uxth	r8, r8
.LVL684:
	b	.L828
.LVL685:
.L832:
	.loc 2 1127 0
	mov	r0, r9
	.loc 2 1128 0
	mov	r9, #0
	.loc 2 1127 0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.LVL686:
.L833:
	.loc 2 1128 0 discriminator 1
	uxth	r3, r9
	cmp	r7, r3
	bls	.L869
	movs	r3, #20
	mul	r10, r3, r9
	.loc 2 1129 0
	ldr	r3, [r4, #3792]
	add	r2, r3, r10
	.loc 2 1132 0
	ldr	r3, [r3, r10]
	.loc 2 1129 0
	ldr	r5, [r2, #4]
	.loc 2 1130 0
	ldr	r6, [r2, #12]
	.loc 2 1132 0
	adds	r3, r3, #1
	.loc 2 1129 0
	ubfx	r5, r5, #10, #16
	.loc 2 1132 0
	bne	.L836
	mov	fp, #16
	.loc 2 1136 0
	movw	r3, #65535
.L838:
	.loc 2 1134 0
	ldr	r0, [r4, #3792]
	str	r3, [sp, #4]
	add	r0, r0, r10
	ldr	r2, [r0, #4]
	adds	r2, r2, #1
	str	r2, [r0, #4]
	.loc 2 1135 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL687:
	.loc 2 1136 0
	ldrh	r2, [r6]
	ldr	r3, [sp, #4]
	cmp	r2, r3
	bne	.L835
	.loc 2 1137 0
	ldr	r3, [r4, #3792]
	mov	r2, #-1
	str	r2, [r3, r10]
	.loc 2 1145 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L836
.L837:
	.loc 2 1287 0
	movs	r1, #1
	b	.L908
.L835:
	.loc 2 1139 0
	ldr	r2, [r4, #3792]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L836
	add	fp, fp, #-1
	uxth	fp, fp
	.loc 2 1133 0 discriminator 2
	cmp	fp, #0
	bne	.L838
	b	.L837
.L836:
	.loc 2 1147 0
	ldr	r2, [r4, #2472]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L839
	.loc 2 1147 0 is_stmt 0 discriminator 1
	cmp	r2, r3
	bhi	.L840
.L839:
	.loc 2 1149 0 is_stmt 1
	adds	r2, r3, #1
	.loc 2 1152 0
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2472]
.L840:
	.loc 2 1157 0
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L842
	bhi	.L843
	movw	r3, #61574
	cmp	r2, r3
	beq	.L844
.L841:
	add	r9, r9, #1
	b	.L833
.L843:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L845
	movw	r3, #65535
	cmp	r2, r3
	bne	.L841
	.loc 2 1280 0
	movs	r1, #0
.L908:
	.loc 2 1287 0
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
.LVL688:
	b	.L841
.L845:
	.loc 2 1162 0
	ldrh	r2, [r4, #3904]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L847
	.loc 2 1162 0 is_stmt 0 discriminator 1
	movw	r2, #1162
	ldr	r1, .L909+8
	ldr	r0, .L909+12
	bl	printk
.LVL689:
.L847:
	.loc 2 1163 0 is_stmt 1
	ldr	r1, [r4, #104]
	ldrh	r0, [r4, #3904]
	.loc 2 1165 0
	ldr	r10, [r4, #3852]
	.loc 2 1163 0
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L848:
	.loc 2 1163 0 is_stmt 0 discriminator 1
	cmp	r3, ip
	bgt	.L854
	.loc 2 1186 0 is_stmt 1
	cmp	r3, #0
	bge	.L886
	b	.L841
.L854:
	.loc 2 1165 0
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L849
	.loc 2 1167 0
	ldr	r2, [r10]
	cbnz	r2, .L850
	.loc 2 1167 0 is_stmt 0 discriminator 1
	cmp	r1, r0
	.loc 2 1172 0 is_stmt 1 discriminator 1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3904]	@ movhi
.L850:
	.loc 2 1175 0 discriminator 1
	uxth	ip, r3
	movs	r2, #0
.L851:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L852
	.loc 2 1180 0
	ldr	r2, [r4, #3852]
	.loc 2 1186 0
	cmp	r3, #0
	.loc 2 1180 0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	.loc 2 1181 0
	ldr	r2, [r4, #3828]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	.loc 2 1186 0
	blt	.L841
	.loc 2 1193 0
	ldrh	r0, [r4, #3904]
	ldr	r2, [r4, #104]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L841
.L886:
	.loc 2 1196 0
	ldr	r2, [r4, #3852]
	.loc 2 1195 0
	adds	r0, r0, #1
	.loc 2 1196 0
	ldr	r1, [r6, #4]
	.loc 2 1195 0
	strh	r0, [r4, #3904]	@ movhi
	.loc 2 1196 0
	str	r1, [r2, r3, lsl #2]
	.loc 2 1197 0
	ldr	r2, [r4, #3828]
.L906:
	.loc 2 1241 0
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L841
.L852:
	.loc 2 1177 0 discriminator 3
	ldr	r0, [r4, #3852]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	.loc 2 1178 0 discriminator 3
	ldr	r0, [r4, #3828]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L851
.L849:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L848
.L910:
	.align	2
.L909:
	.word	.LANCHOR0
	.word	.LANCHOR0+36
	.word	.LANCHOR1+328
	.word	.LC1
.L844:
	.loc 2 1203 0
	ldrh	r2, [r4, #120]
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bls	.L857
	.loc 2 1203 0 is_stmt 0 discriminator 1
	movw	r2, #1203
	ldr	r1, .L911
	ldr	r0, .L911+4
	bl	printk
.LVL690:
.L857:
	.loc 2 1204 0 is_stmt 1
	ldrh	lr, [r4, #96]
	ldrh	r0, [r4, #120]
	.loc 2 1206 0
	ldr	r10, [r4, #3840]
	.loc 2 1204 0
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L858:
	.loc 2 1204 0 is_stmt 0 discriminator 1
	cmp	r3, ip
	ble	.L863
	.loc 2 1206 0 is_stmt 1
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L859
	.loc 2 1208 0
	ldr	r2, [r10]
	cbnz	r2, .L860
	.loc 2 1208 0 is_stmt 0 discriminator 1
	cmp	lr, r0
	.loc 2 1216 0 is_stmt 1 discriminator 1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #120]	@ movhi
.L860:
	.loc 2 1218 0 discriminator 1
	uxth	ip, r3
	movs	r2, #0
.L861:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L862
	.loc 2 1223 0
	ldr	r2, [r4, #3840]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	.loc 2 1224 0
	ldr	r2, [r4, #124]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L863:
	.loc 2 1229 0
	cmp	r3, #0
	blt	.L841
	.loc 2 1237 0
	ldrh	r2, [r4, #96]
	ldrh	r1, [r4, #120]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L841
	.loc 2 1239 0
	adds	r1, r1, #1
	.loc 2 1240 0
	ldr	r2, [r4, #3840]
	.loc 2 1239 0
	strh	r1, [r4, #120]	@ movhi
	.loc 2 1240 0
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	.loc 2 1241 0
	ldr	r2, [r4, #124]
	b	.L906
.L862:
	.loc 2 1220 0 discriminator 3
	ldr	r0, [r4, #3840]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	.loc 2 1221 0 discriminator 3
	ldr	r0, [r4, #124]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L861
.L859:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L858
.L842:
	.loc 2 1248 0
	ldrh	r1, [r4, #2556]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L865
	.loc 2 1250 0
	strh	r5, [r4, #2556]	@ movhi
.L907:
	.loc 2 1261 0
	str	r3, [r4, #2564]
	b	.L841
.L865:
	.loc 2 1255 0
	ldrh	r0, [r4, #2560]
	cmp	r0, r2
	beq	.L866
	.loc 2 1256 0
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.LVL691:
.L866:
	.loc 2 1257 0
	ldr	r2, [r4, #2564]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L867
	.loc 2 1259 0
	ldrh	r3, [r4, #2556]
	.loc 2 1260 0
	strh	r5, [r4, #2556]	@ movhi
	.loc 2 1259 0
	strh	r3, [r4, #2560]	@ movhi
	.loc 2 1261 0
	ldr	r3, [r6, #4]
	b	.L907
.L867:
	.loc 2 1265 0
	strh	r5, [r4, #2560]	@ movhi
	b	.L841
.L870:
	.loc 2 1292 0
	ldr	r1, [r4, #3828]
	ldrh	r3, [r1]
	cbz	r3, .L872
.L875:
	.loc 2 1310 0
	ldr	r1, [r4, #124]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L873
.L874:
	.loc 2 1328 0
	ldrh	r2, [r4, #3904]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L905
	.loc 2 1328 0 is_stmt 0 discriminator 1
	mov	r2, #1328
	ldr	r1, .L911
	ldr	r0, .L911+4
	bl	printk
.LVL692:
.L905:
	.loc 2 1330 0 is_stmt 1
	movs	r0, #0
	add	sp, sp, #20
.LCFI110:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL693:
.L872:
.LCFI111:
	.loc 2 1292 0 discriminator 1
	ldrh	r2, [r4, #3904]
	cmp	r2, #0
	beq	.L875
	.loc 2 1294 0 discriminator 1
	ldr	r0, [r4, #104]
.L876:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L875
	.loc 2 1296 0
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L876
	mov	r3, r2
	.loc 2 1302 0 discriminator 1
	movs	r5, #0
.L877:
	.loc 2 1298 0 discriminator 1
	ldr	r1, [r4, #104]
	cmp	r3, r1
	bcs	.L875
	.loc 2 1300 0 discriminator 3
	ldr	r1, [r4, #3828]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
.LVL694:
	strh	r6, [r1, r0, lsl #1]	@ movhi
	.loc 2 1301 0 discriminator 3
	ldr	r1, [r4, #3852]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	.loc 2 1302 0 discriminator 3
	ldr	r1, [r4, #3828]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L877
.LVL695:
.L873:
	.loc 2 1310 0 discriminator 1
	ldrh	r3, [r4, #120]
	cmp	r3, #0
	beq	.L874
	.loc 2 1312 0 discriminator 1
	ldrh	r0, [r4, #96]
.L882:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L874
	.loc 2 1314 0
	ldrh	r6, [r1, r3, lsl #1]
.LVL696:
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L882
	.loc 2 1320 0 discriminator 1
	movs	r0, #0
.L883:
	.loc 2 1316 0 discriminator 1
	ldrh	r2, [r4, #96]
	cmp	r3, r2
	bge	.L874
	.loc 2 1318 0 discriminator 3
	ldr	r2, [r4, #124]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	.loc 2 1319 0 discriminator 3
	ldr	r2, [r4, #3840]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	.loc 2 1320 0 discriminator 3
	ldr	r2, [r4, #124]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L883
.L912:
	.align	2
.L911:
	.word	.LANCHOR1+328
	.word	.LC1
.LFE220:
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	1
	.global	FtlMapTblRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
.LFB225:
	.loc 2 1466 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL697:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI112:
	.pad #28
	sub	sp, sp, #28
.LCFI113:
	.loc 2 1470 0
	ldr	r3, [r0, #24]
.LVL698:
	.loc 2 1466 0
	mov	r4, r0
	.loc 2 1477 0
	movs	r1, #0
	.loc 2 1473 0
	ldrh	r10, [r0, #6]
.LVL699:
	.loc 2 1470 0
	str	r3, [sp, #4]
	.loc 2 1472 0
	ldr	r3, [r0, #16]
.LVL700:
	.loc 2 1478 0
	ldr	r5, .L940
	.loc 2 1471 0
	ldr	r9, [r0, #12]
.LVL701:
	.loc 2 1477 0
	lsl	r2, r10, #2
	.loc 2 1472 0
	str	r3, [sp, #12]
	.loc 2 1474 0
	ldrh	r3, [r0, #8]
.LVL702:
	.loc 2 1477 0
	ldr	r0, [sp, #4]
.LVL703:
	.loc 2 1474 0
	str	r3, [sp, #8]
	.loc 2 1477 0
	bl	ftl_memset
.LVL704:
	.loc 2 1478 0
	ldr	r3, [r5, #2244]
	.loc 2 1485 0
	movs	r2, #1
	.loc 2 1479 0
	ldr	r6, [r5, #3244]
	.loc 2 1485 0
	str	r2, [r4, #36]
	.loc 2 1478 0
	str	r3, [r5, #3232]
	.loc 2 1481 0
	movw	r3, #65535
	.loc 2 1479 0
	str	r6, [r5, #3236]
	.loc 2 1481 0
	strh	r3, [r4]	@ movhi
	.loc 2 1482 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 1483 0
	movs	r3, #0
	str	r3, [r4, #32]
	.loc 2 1486 0
	mov	r8, r3
	.loc 2 1484 0
	str	r3, [r4, #28]
	.loc 2 1488 0
	ldr	r3, [sp, #8]
.LVL705:
	add	fp, r3, #-1
	.loc 2 1530 0
	mov	r3, r5
.LVL706:
.L914:
	.loc 2 1486 0 discriminator 1
	ldr	r1, [sp, #8]
.LVL707:
	sxth	r2, r8
.LVL708:
	cmp	r2, r1
	bge	.L931
	.loc 2 1488 0
	cmp	r2, fp
	lsl	r7, r2, #1
.LVL709:
	bne	.L915
	.loc 2 1490 0
	ldrh	r0, [r9, fp, lsl #1]
.LVL710:
	movs	r1, #1
.LVL711:
	bl	FtlGetLastWrittenPage
.LVL712:
	.loc 2 1493 0
	ldr	r3, [sp, #12]
.LVL713:
	.loc 2 1490 0
	add	r7, r7, r9
	.loc 2 1492 0
	strh	r8, [r4]	@ movhi
	.loc 2 1490 0
	sxth	r9, r0
.LVL714:
	.loc 2 1497 0
	ldr	r8, .L940+4
	.loc 2 1491 0
	adds	r0, r0, #1
	strh	r0, [r4, #2]	@ movhi
	.loc 2 1494 0
	add	r9, r9, #1
.LVL715:
	.loc 2 1493 0
	ldr	r3, [r3, fp, lsl #2]
.LVL716:
	.loc 2 1494 0
	mov	fp, #0
	.loc 2 1493 0
	str	r3, [r4, #28]
.LVL717:
.L916:
	.loc 2 1494 0 discriminator 1
	sxth	r2, fp
	cmp	r2, r9
	blt	.L918
.LVL718:
.L931:
	.loc 2 1547 0
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
.LVL719:
	.loc 2 1548 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bne	.L920
	.loc 2 1550 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL720:
.L920:
	.loc 2 1553 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL721:
	.loc 2 1555 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL722:
	.loc 2 1557 0
	movs	r0, #0
	add	sp, sp, #28
.LCFI114:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL723:
.L918:
.LCFI115:
	.loc 2 1496 0
	ldrh	r1, [r7]
	.loc 2 1497 0
	mov	r0, r8
	.loc 2 1496 0
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3228]
	.loc 2 1497 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL724:
	.loc 2 1500 0
	ldr	r2, [r5, #3224]
	adds	r2, r2, #1
	beq	.L917
	.loc 2 1502 0
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L917
	.loc 2 1502 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	.loc 2 1504 0 is_stmt 1 discriminator 1
	ittt	eq
	ldreq	r1, [r5, #3228]
	ldreq	r3, [sp, #4]
.LVL725:
	streq	r1, [r3, r2, lsl #2]
.L917:
	add	fp, fp, #1
	b	.L916
.LVL726:
.L915:
	.loc 2 1510 0
	ldr	r1, [r5, #2244]
.LVL727:
	str	r3, [sp, #20]
	.loc 2 1511 0
	add	r3, r9, r7
	.loc 2 1512 0
	ldr	r0, .L940+4
.LVL728:
	.loc 2 1510 0
	str	r1, [r5, #3232]
	.loc 2 1511 0
	ldrh	r1, [r9, r2, lsl #1]
	ldrh	r2, [r5, #80]
.LVL729:
	str	r3, [sp, #16]
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3228]
	.loc 2 1512 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL730:
	.loc 2 1515 0
	ldr	r2, [r5, #3224]
	ldr	r3, [sp, #20]
	adds	r2, r2, #1
	beq	.L933
	.loc 2 1515 0 is_stmt 0 discriminator 1
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L933
	.loc 2 1515 0 discriminator 2
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L922
.L933:
	movs	r7, #0
.LVL731:
.L923:
	.loc 2 1530 0 is_stmt 1 discriminator 1
	ldrh	r1, [r3, #80]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L929
	.loc 2 1532 0
	ldr	r1, [sp, #16]
	.loc 2 1533 0
	ldr	r0, .L940+4
.LVL732:
	.loc 2 1532 0
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	str	r2, [r3, #3228]
	.loc 2 1533 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL733:
	.loc 2 1536 0
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #3224]
	adds	r2, r2, #1
	beq	.L927
	.loc 2 1538 0
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L927
	.loc 2 1538 0 is_stmt 0 discriminator 1
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	.loc 2 1540 0 is_stmt 1 discriminator 1
	ittt	eq
	ldreq	r1, [r3, #3228]
	ldreq	r0, [sp, #4]
.LVL734:
	streq	r1, [r0, r2, lsl #2]
.L927:
	adds	r7, r7, #1
	b	.L923
.LVL735:
.L922:
	movs	r0, #0
	.loc 2 1523 0 discriminator 1
	mov	ip, #4
.LVL736:
.L924:
	.loc 2 1518 0 discriminator 1
	ldrh	r1, [r5, #80]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L926
.L929:
	add	r8, r8, #1
	b	.L914
.L926:
	.loc 2 1520 0
	ldr	r7, [r5, #2244]
.LVL737:
	ldr	r1, [r7, r2, lsl #3]
	.loc 2 1521 0
	uxth	lr, r1
	cmp	r10, lr
	bls	.L925
	.loc 2 1523 0
	add	r2, ip, r2, lsl #3
	ldr	r2, [r7, r2]
	ldr	r7, [sp, #4]
.LVL738:
	str	r2, [r7, lr, lsl #2]
.L925:
	adds	r0, r0, #1
	b	.L924
.L941:
	.align	2
.L940:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
.LFE225:
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	1
	.global	FtlLoadVonderInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
.LFB226:
	.loc 2 1560 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	.loc 2 1561 0
	ldr	r0, .L943
	.loc 2 1560 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI116:
	.loc 2 1561 0
	ldrh	r3, [r0, #96]
	strh	r3, [r0, #3918]	@ movhi
	.loc 2 1562 0
	movw	r3, #61574
	strh	r3, [r0, #3912]	@ movhi
	.loc 2 1563 0
	ldrh	r3, [r0, #120]
	strh	r3, [r0, #3916]	@ movhi
	.loc 2 1564 0
	ldrh	r3, [r0, #98]
	strh	r3, [r0, #3914]	@ movhi
	.loc 2 1565 0
	ldr	r3, [r0, #124]
	.loc 2 1570 0
	addw	r0, r0, #3908
	.loc 2 1565 0
	str	r3, [r0, #12]
	.loc 2 1566 0
	ldr	r3, [r0, #-68]
	str	r3, [r0, #16]
	.loc 2 1567 0
	ldr	r3, [r0, #-72]
	str	r3, [r0, #20]
	.loc 2 1568 0
	ldr	r3, [r0, #-64]
	str	r3, [r0, #24]
	.loc 2 1570 0
	bl	FtlMapTblRecovery
.LVL739:
	.loc 2 1573 0
	movs	r0, #0
	pop	{r3, pc}
.L944:
	.align	2
.L943:
	.word	.LANCHOR0
.LFE226:
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	1
	.global	FtlL2PDataInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
.LFB227:
	.loc 2 1576 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI117:
	.loc 2 1578 0
	movs	r1, #0
	ldr	r4, .L948
	.loc 2 1582 0
	movs	r6, #12
	.loc 2 1578 0
	ldr	r2, [r4, #104]
	ldr	r0, [r4, #3832]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL740:
	.loc 2 1579 0
	ldrh	r3, [r4, #86]
	movs	r1, #255
	ldrh	r2, [r4, #114]
	ldr	r0, [r4, #3856]
	muls	r2, r3, r2
	bl	ftl_memset
.LVL741:
	.loc 2 1580 0
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	.loc 2 1582 0
	mov	r7, r2
.L946:
	.loc 2 1580 0 discriminator 1
	ldrh	r4, [r3, #114]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L947
	.loc 2 1588 0
	ldr	r2, [r3, #104]
	.loc 2 1586 0
	strh	r1, [r3, #3866]	@ movhi
	.loc 2 1587 0
	strh	r1, [r3, #3864]	@ movhi
	.loc 2 1588 0
	strh	r2, [r3, #3874]	@ movhi
	.loc 2 1589 0
	movw	r2, #61634
	strh	r2, [r3, #3868]	@ movhi
	.loc 2 1590 0
	ldrh	r2, [r3, #3904]
	strh	r2, [r3, #3872]	@ movhi
	.loc 2 1591 0
	ldrh	r2, [r3, #112]
	strh	r2, [r3, #3870]	@ movhi
	.loc 2 1592 0
	ldr	r2, [r3, #3828]
	str	r2, [r3, #3876]
	.loc 2 1593 0
	ldr	r2, [r3, #3852]
	str	r2, [r3, #3880]
	.loc 2 1594 0
	ldr	r2, [r3, #3832]
	str	r2, [r3, #3884]
	.loc 2 1595 0
	ldr	r2, [r3, #3848]
	str	r2, [r3, #3888]
	pop	{r3, r4, r5, r6, r7, pc}
.L947:
	.loc 2 1582 0 discriminator 3
	uxth	r2, r2
	ldr	r0, [r3, #2424]
	mul	r4, r6, r2
	add	ip, r0, r4
	str	r7, [ip, #4]
	.loc 2 1583 0 discriminator 3
	strh	r1, [r0, r4]	@ movhi
	.loc 2 1584 0 discriminator 3
	ldr	r0, [r3, #2424]
	add	r0, r0, r4
	ldrh	r4, [r3, #86]
	muls	r2, r4, r2
	ldr	r4, [r3, #3856]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L946
.L949:
	.align	2
.L948:
	.word	.LANCHOR0
.LFE227:
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	1
	.global	FtlLoadMapInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
.LFB228:
	.loc 2 1599 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI118:
	.loc 2 1600 0
	bl	FtlL2PDataInit
.LVL742:
	.loc 2 1602 0
	ldr	r0, .L951
	bl	FtlMapTblRecovery
.LVL743:
	.loc 2 1605 0
	movs	r0, #0
	pop	{r3, pc}
.L952:
	.align	2
.L951:
	.word	.LANCHOR0+3864
.LFE228:
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	1
	.global	FtlVariablesInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
.LFB157:
	.loc 3 493 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI119:
	.loc 3 496 0
	movs	r5, #0
	ldr	r4, .L954
	.loc 3 497 0
	movw	r3, #65535
	.loc 3 502 0
	mov	r1, r5
	ldrh	r2, [r4, #96]
	.loc 3 497 0
	strh	r3, [r4, #3952]	@ movhi
	.loc 3 500 0
	mov	r3, #-1
	.loc 3 502 0
	ldr	r0, [r4, #124]
	.loc 3 500 0
	str	r3, [r4, #3964]
	.loc 3 502 0
	lsls	r2, r2, #1
	.loc 3 496 0
	str	r5, [r4, #3948]
	.loc 3 498 0
	str	r5, [r4, #3956]
	.loc 3 499 0
	str	r5, [r4, #3960]
	.loc 3 501 0
	strh	r5, [r4, #120]	@ movhi
	.loc 3 502 0
	bl	ftl_memset
.LVL744:
	.loc 3 503 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #2248]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL745:
	.loc 3 504 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #3816]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL746:
.LBB408:
.LBB409:
.LBB410:
	.loc 1 202 0
	addw	r0, r4, #2504
	movs	r1, #48
	bl	__memzero
.LVL747:
.LBE410:
.LBE409:
.LBE408:
.LBB411:
.LBB412:
.LBB413:
	addw	r0, r4, #2616
	mov	r1, #512
	bl	__memzero
.LVL748:
.LBE413:
.LBE412:
.LBE411:
	.loc 3 507 0
	bl	FtlGcBufInit
.LVL749:
	.loc 3 508 0
	bl	FtlL2PDataInit
.LVL750:
	.loc 3 510 0
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L955:
	.align	2
.L954:
	.word	.LANCHOR0
.LFE157:
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	1
	.global	SupperBlkListInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
.LFB235:
	.loc 2 2067 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI120:
	.loc 2 2072 0
	movs	r6, #0
	.loc 2 2071 0
	ldr	r4, .L967
	movs	r2, #6
	.loc 2 2077 0
	mov	r9, r6
.LVL751:
	mov	r7, r6
.LVL752:
	.loc 2 2071 0
	movs	r1, #0
	ldrh	r3, [r4, #18]
	.loc 2 2082 0
	add	fp, r4, #36
	.loc 2 2071 0
	ldr	r0, [r4, #2252]
	muls	r2, r3, r2
	bl	ftl_memset
.LVL753:
	.loc 2 2072 0
	str	r6, [r4, #2272]
	.loc 2 2073 0
	str	r6, [r4, #2256]
	.loc 2 2074 0
	str	r6, [r4, #2264]
	.loc 2 2075 0
	strh	r6, [r4, #2268]	@ movhi
	.loc 2 2076 0
	strh	r6, [r4, #2276]	@ movhi
.LVL754:
.L957:
	.loc 2 2079 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r8, r6
	cmp	r8, r3
	bcs	.L963
	.loc 2 2084 0
	mov	r10, #0
	.loc 2 2081 0
	ldrh	r2, [r4, #8]
	.loc 2 2084 0
	ldrh	r3, [r4, #78]
	mov	r5, r10
	b	.L964
.L959:
	.loc 2 2082 0
	mov	r1, r8
	ldrb	r0, [fp, r10]	@ zero_extendqisi2
	stm	sp, {r2, r3}
	bl	V2P_block
.LVL755:
	.loc 2 2083 0
	bl	FtlBbmIsBadBlock
.LVL756:
	ldm	sp, {r2, r3}
	cbnz	r0, .L958
	.loc 2 2084 0
	add	r5, r5, r3
	uxth	r5, r5
.L958:
	add	r10, r10, #1
.L964:
	.loc 2 2081 0 discriminator 1
	uxth	r1, r10
	cmp	r2, r1
	bhi	.L959
	.loc 2 2086 0
	cbz	r5, .L966
	.loc 2 2087 0
	mov	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
.LVL757:
.L960:
	.loc 2 2088 0
	ldr	r2, [r4, #2252]
	uxth	r1, r6
	movs	r3, #6
	mla	r3, r3, r1, r2
	strh	r0, [r3, #4]	@ movhi
	.loc 2 2090 0
	ldrh	r3, [r4, #2280]
	cmp	r3, r8
	beq	.L961
	.loc 2 2090 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2328]
	cmp	r3, r8
	beq	.L961
	.loc 2 2091 0 is_stmt 1
	ldrh	r3, [r4, #2376]
	cmp	r3, r8
	beq	.L961
	.loc 2 2094 0
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r1, lsl #1]
	cbnz	r3, .L962
	.loc 2 2095 0
	add	r9, r9, #1
.LVL758:
	.loc 2 2096 0
	mov	r0, r8
	.loc 2 2095 0
	uxth	r9, r9
.LVL759:
	.loc 2 2096 0
	bl	INSERT_FREE_LIST
.LVL760:
.L961:
	adds	r6, r6, #1
	b	.L957
.L966:
	mov	r0, r5
	b	.L960
.L962:
	.loc 2 2098 0
	adds	r7, r7, #1
.LVL761:
	.loc 2 2099 0
	mov	r0, r8
	.loc 2 2098 0
	uxth	r7, r7
.LVL762:
	.loc 2 2099 0
	bl	INSERT_DATA_LIST
.LVL763:
	b	.L961
.L963:
	.loc 2 2103 0
	strh	r7, [r4, #2268]	@ movhi
	.loc 2 2105 0
	add	r7, r7, r9
.LVL764:
	cmp	r7, r3
	.loc 2 2104 0
	strh	r9, [r4, #2276]	@ movhi
	.loc 2 2105 0
	ble	.L965
	.loc 2 2105 0 is_stmt 0 discriminator 1
	movw	r2, #2105
	ldr	r1, .L967+4
	ldr	r0, .L967+8
	bl	printk
.LVL765:
.L965:
	.loc 2 2107 0 is_stmt 1
	movs	r0, #0
	add	sp, sp, #12
.LCFI121:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL766:
.L968:
	.align	2
.L967:
	.word	.LANCHOR0
	.word	.LANCHOR1+342
	.word	.LC1
.LFE235:
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	1
	.global	FtlGcPageVarInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
.LFB259:
	.loc 5 202 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI122:
	.loc 5 203 0
	movs	r3, #0
	ldr	r4, .L970
	.loc 5 205 0
	movs	r1, #255
	ldrh	r2, [r4, #82]
	ldr	r0, [r4, #3168]
	.loc 5 203 0
	strh	r3, [r4, #3172]	@ movhi
	.loc 5 204 0
	strh	r3, [r4, #3180]	@ movhi
	.loc 5 205 0
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL767:
	.loc 5 206 0
	ldrh	r3, [r4, #82]
	movs	r2, #12
	ldr	r0, [r4, #3176]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
.LVL768:
	.loc 5 208 0
	pop	{r4, lr}
.LCFI123:
	.loc 5 207 0
	b	FtlGcBufInit
.LVL769:
.L971:
	.align	2
.L970:
	.word	.LANCHOR0
.LFE259:
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	1
	.global	FtlGcScanTempBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
.LFB260:
	.loc 5 211 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL770:
	.loc 5 220 0
	ldr	r3, .L997
	.loc 5 211 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI124:
	.pad #28
	sub	sp, sp, #28
.LCFI125:
	.loc 5 211 0
	mov	r7, r0
	str	r1, [sp, #8]
	.loc 5 220 0
	ldrh	r4, [r3, #4]
.LVL771:
	.loc 5 221 0
	movw	r3, #65535
	cmp	r4, r3
	beq	.L991
	.loc 5 223 0
	cbnz	r4, .L973
.LVL772:
.L974:
	.loc 5 224 0
	bl	FtlGcPageVarInit
.LVL773:
	b	.L975
.LVL774:
.L991:
	.loc 5 222 0
	movs	r4, #0
.L973:
	.loc 5 223 0 discriminator 1
	ldr	r3, .L997+4
	ldr	r2, [sp, #8]
.LVL775:
	ldrh	r3, [r3, #78]
	cmp	r3, r2
	beq	.L974
.L975:
	.loc 5 222 0
	mov	fp, #0
.LVL776:
	.loc 5 228 0
	movw	r8, #65535
.LVL777:
.L985:
	ldrh	r3, [r7]
	.loc 5 226 0
	movs	r0, #0
	strb	r0, [r7, #8]
	.loc 5 228 0
	cmp	r3, r8
	beq	.L976
	.loc 5 231 0
	ldr	r5, .L997+4
.L988:
	.loc 5 234 0
	ldr	r3, [r5, #3792]
	.loc 5 231 0
	movs	r2, #0
.LVL778:
	ldrh	ip, [r5, #8]
	add	lr, r7, #16
	.loc 5 236 0
	ldr	r10, [r5, #3156]
	.loc 5 230 0
	mov	r6, r2
	.loc 5 234 0
	str	r3, [sp, #4]
	mov	r9, #20
	.loc 5 235 0
	ldr	r3, [r5, #3152]
	str	r3, [sp, #12]
	ldrh	r3, [r5, #86]
	str	r3, [sp, #16]
	.loc 5 236 0
	ldrh	r3, [r5, #88]
	str	r3, [sp, #20]
.LVL779:
.L977:
	.loc 5 231 0 discriminator 1
	uxth	r3, r2
.LVL780:
	cmp	ip, r3
	bhi	.L981
	.loc 5 241 0
	mov	r9, #0
	.loc 5 240 0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [sp, #4]
	bl	FlashReadPages
.LVL781:
.L982:
	.loc 5 241 0 discriminator 1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L986
	.loc 5 268 0
	ldr	r3, [sp, #8]
.LVL782:
	.loc 5 266 0
	add	fp, fp, #1
	.loc 5 264 0
	adds	r4, r4, #1
.LVL783:
	uxth	r4, r4
.LVL784:
	.loc 5 268 0
	cmp	r3, fp
	bls	.L987
.L989:
	.loc 5 274 0
	ldrh	r3, [r5, #78]
.LVL785:
	cmp	r3, r4
	bhi	.L988
	.loc 5 265 0
	movs	r0, #0
.LVL786:
	b	.L976
.LVL787:
.L981:
	.loc 5 232 0
	ldrh	r3, [lr], #2
.LVL788:
	.loc 5 233 0
	cmp	r3, r8
	beq	.L978
	.loc 5 234 0
	ldr	r1, [sp, #4]
	orr	r3, r4, r3, lsl #10
.LVL789:
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	.loc 5 235 0
	ldr	r3, [sp, #16]
	muls	r3, r6, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r0, r3
	ldr	r3, [sp, #12]
	add	r0, r0, r3
	.loc 5 236 0
	ldr	r3, [sp, #20]
	muls	r3, r6, r3
	.loc 5 237 0
	add	r6, r6, #1
.LVL790:
	.loc 5 236 0
	it	mi
	addmi	r3, r3, #3
	.loc 5 237 0
	uxth	r6, r6
.LVL791:
	.loc 5 236 0
	bic	r3, r3, #3
	.loc 5 235 0
	str	r0, [r1, #8]
	.loc 5 236 0
	add	r3, r3, r10
	str	r3, [r1, #12]
.LVL792:
.L978:
	adds	r2, r2, #1
	b	.L977
.LVL793:
.L986:
	.loc 5 242 0
	movs	r3, #20
	ldr	r2, [r5, #3792]
	mul	r10, r3, r9
	str	r2, [sp, #16]
	add	r3, r2, r10
	ldr	r1, [r3, #4]
.LVL794:
	str	r3, [sp, #12]
	.loc 5 243 0
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_plane
.LVL795:
	.loc 5 245 0
	ldr	r2, [sp, #16]
	ldr	r2, [r2, r10]
	cbnz	r2, .L983
	.loc 5 247 0
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	.loc 5 248 0
	ldr	r1, [sp, #4]
.LVL796:
	.loc 5 247 0
	ldr	r3, [r3, #12]
.LVL797:
	.loc 5 248 0
	ldrh	r2, [r3]
	cmp	r2, r8
	bne	.L984
	.loc 5 249 0
	movs	r3, #1
.LVL798:
	str	r3, [r5, #3960]
.LVL799:
.L976:
	.loc 5 281 0
	ldr	r3, .L997
	movw	r2, #65535
.LVL800:
	.loc 5 283 0
	strb	r0, [r7, #6]
	.loc 5 284 0
	mov	r1, r4
.LVL801:
	.loc 5 282 0
	strh	r4, [r7, #2]	@ movhi
	.loc 5 281 0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 284 0
	mov	r2, r0
	mov	r0, r7
.LVL802:
	bl	ftl_sb_update_avl_pages
.LVL803:
	.loc 5 285 0
	b	.L990
.LVL804:
.L984:
	.loc 5 252 0
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL805:
	b	.L982
.L983:
	.loc 5 256 0
	ldrh	r2, [r7]
	movs	r4, #0
.LVL806:
	ldr	r3, [r5, #2260]
	strh	r4, [r3, r2, lsl #1]	@ movhi
	.loc 5 257 0
	ldrh	r0, [r7]
	bl	INSERT_FREE_LIST
.LVL807:
	.loc 5 258 0
	strh	r8, [r7]	@ movhi
	.loc 5 260 0
	bl	FtlGcPageVarInit
.LVL808:
	.loc 5 261 0
	b	.L985
.LVL809:
.L987:
	.loc 5 268 0 discriminator 1
	ldr	r2, .L997
	ldrh	r3, [r2, #4]
.LVL810:
	cmp	r3, r8
	beq	.L989
	.loc 5 269 0
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	.loc 5 270 0
	ldrh	r3, [r5, #78]
	cmp	r3, r4
	bls	.L989
.L990:
	.loc 5 286 0
	mov	r0, #-1
	add	sp, sp, #28
.LCFI126:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL811:
.L998:
	.align	2
.L997:
	.word	.LANCHOR2
	.word	.LANCHOR0
.LFE260:
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	1
	.global	ftl_memcpy
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_memcpy, %function
ftl_memcpy:
	.fnstart
.LFB278:
	.loc 1 206 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL812:
	.loc 1 207 0
	b	memcpy
.LVL813:
.LFE278:
	.fnend
	.size	ftl_memcpy, .-ftl_memcpy
	.align	1
	.global	FtlBbmTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
.LFB176:
	.loc 4 80 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI127:
	.loc 4 91 0
	movs	r5, #0
.LVL814:
	.loc 4 87 0
	ldr	r4, .L1009
	.loc 4 89 0
	movs	r1, #0
	.loc 4 87 0
	ldr	r0, [r4, #2244]
	add	r6, r4, #152
	.loc 4 88 0
	ldr	r3, [r4, #3244]
	.loc 4 89 0
	ldrh	r2, [r4, #86]
	.loc 4 87 0
	str	r0, [r4, #3232]
	.loc 4 88 0
	str	r3, [r4, #3236]
	.loc 4 89 0
	bl	ftl_memset
.LVL815:
.L1001:
	.loc 4 91 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	blt	.L1002
	.loc 4 94 0
	ldr	r6, [r4, #3236]
.LVL816:
.LBB419:
.LBB420:
.LBB421:
	.loc 1 202 0
	movs	r2, #16
	movs	r1, #255
.LBE421:
.LBE420:
.LBE419:
	.loc 4 108 0
	ldr	r9, .L1009+12
	.loc 4 109 0
	ldr	r8, .L1009+4
	.loc 4 84 0
	movs	r5, #0
.LVL817:
.LBB426:
.LBB424:
.LBB422:
	.loc 1 202 0
	mov	r0, r6
.LBE422:
.LBE424:
.LBE426:
	.loc 4 129 0
	ldr	r10, .L1009+16
.LBB427:
.LBB425:
.LBB423:
	.loc 1 202 0
	bl	memset
.LVL818:
.LBE423:
.LBE425:
.LBE427:
	.loc 4 97 0
	movw	r3, #61649
	.loc 4 82 0
	mov	r7, r5
.LVL819:
	.loc 4 97 0
	strh	r3, [r6]	@ movhi
	.loc 4 98 0
	ldr	r3, [r4, #136]
	str	r3, [r6, #4]
	.loc 4 99 0
	ldrh	r3, [r4, #128]
	strh	r3, [r6, #2]	@ movhi
	.loc 4 100 0
	ldrh	r3, [r4, #132]
	strh	r3, [r6, #8]	@ movhi
	.loc 4 101 0
	ldrh	r3, [r4, #134]
	strh	r3, [r6, #10]	@ movhi
	.loc 4 102 0
	ldr	r3, [r4, #4]
	strh	r3, [r6, #12]	@ movhi
.L1003:
	.loc 4 104 0
	ldr	r3, [r4, #2244]
	.loc 4 106 0
	mov	fp, #0
	.loc 4 107 0
	ldrh	r1, [r4, #128]
	ldrh	r2, [r4, #130]
	.loc 4 104 0
	str	r3, [r4, #3232]
	.loc 4 105 0
	ldr	r3, [r4, #3244]
	.loc 4 106 0
	str	fp, [r4, #3224]
	.loc 4 105 0
	str	r3, [r4, #3236]
	.loc 4 107 0
	orr	r3, r2, r1, lsl #10
	.loc 4 108 0
	ldrh	r0, [r6, #10]
	.loc 4 107 0
	str	r3, [r4, #3228]
	.loc 4 108 0
	ldrh	r3, [r4, #132]
	str	r0, [sp]
	mov	r0, r9
	bl	printk
.LVL820:
	.loc 4 109 0
	movs	r3, #1
	ldr	r0, .L1009+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL821:
	.loc 4 110 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r4, #130]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1004
	.loc 4 113 0
	ldr	r3, [r4, #136]
	.loc 4 115 0
	ldrh	r2, [r4, #128]
	.loc 4 121 0
	ldr	r0, [r4, #3760]
	.loc 4 113 0
	adds	r3, r3, #1
	.loc 4 117 0
	strh	fp, [r4, #130]	@ movhi
	.loc 4 113 0
	str	r3, [r4, #136]
	.loc 4 114 0
	str	r3, [r6, #4]
	.loc 4 116 0
	ldrh	r3, [r4, #132]
	.loc 4 115 0
	strh	r2, [r6, #8]	@ movhi
	.loc 4 118 0
	strh	r2, [r4, #132]	@ movhi
	.loc 4 122 0
	movs	r2, #1
	.loc 4 116 0
	strh	r3, [r4, #128]	@ movhi
	.loc 4 122 0
	mov	r1, r2
	.loc 4 119 0
	lsls	r3, r3, #10
	str	r3, [r4, #3228]
	.loc 4 121 0
	str	r3, [r0, #4]
	.loc 4 122 0
	bl	FlashEraseBlocks
.LVL822:
	.loc 4 123 0
	movs	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL823:
.L1004:
	.loc 4 126 0
	ldrh	r3, [r4, #130]
	adds	r3, r3, #1
	strh	r3, [r4, #130]	@ movhi
	.loc 4 127 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	bne	.L1005
	.loc 4 130 0
	adds	r5, r5, #1
.LVL824:
	.loc 4 129 0
	ldr	r1, [r4, #3228]
	.loc 4 130 0
	uxth	r5, r5
.LVL825:
	.loc 4 129 0
	mov	r0, r10
	bl	printk
.LVL826:
	.loc 4 131 0
	cmp	r5, #3
	bls	.L1003
	.loc 4 132 0
	mov	r2, r5
	ldr	r1, [r4, #3228]
	ldr	r0, .L1009+8
	bl	printk
.LVL827:
.L1007:
	b	.L1007
.LVL828:
.L1002:
	.loc 4 92 0 discriminator 3
	ldrh	r2, [r4, #3860]
	ldr	r3, [r4, #3232]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	.loc 4 91 0 discriminator 3
	adds	r5, r5, #1
	.loc 4 92 0 discriminator 3
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
.LVL829:
	b	.L1001
.LVL830:
.L1008:
	movs	r7, #1
	b	.L1003
.L1005:
	.loc 4 139 0
	cmp	r7, #0
	beq	.L1008
	.loc 4 146 0
	movs	r0, #0
	add	sp, sp, #12
.LCFI128:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL831:
.L1010:
	.align	2
.L1009:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
	.word	.LC92
	.word	.LC90
	.word	.LC91
.LFE176:
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	1
	.global	allocate_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
.LFB248:
	.loc 2 2531 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL832:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI129:
	mov	r5, r0
	ldr	r4, .L1046
	.pad #20
	sub	sp, sp, #20
.LCFI130:
	.loc 2 2537 0
	ldr	r9, .L1046+20
.LVL833:
.L1012:
	ldr	r3, .L1046
	ldr	r2, .L1046
	ldrh	r3, [r3, #2276]
	ldrh	r2, [r2, #2268]
	add	r3, r3, r2
	ldr	r2, .L1046
	ldrh	r2, [r2, #16]
	cmp	r3, r2
	ble	.L1013
	.loc 2 2537 0 is_stmt 0 discriminator 1
	movw	r2, #2537
	mov	r1, r9
	ldr	r0, .L1046+4
	bl	printk
.LVL834:
.L1013:
	.loc 2 2539 0 is_stmt 1
	ldr	r3, .L1046+8
	cmp	r5, r3
	bne	.L1038
	.loc 2 2540 0
	ldrh	r3, [r4, #2276]
	ldr	r1, [r4, #3140]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
.LVL835:
	.loc 2 2544 0
	cbz	r1, .L1014
	.loc 2 2545 0
	subs	r1, r1, #1
.LVL836:
	uxth	r1, r1
.LVL837:
.L1014:
	.loc 2 2546 0
	ldr	r0, .L1046+12
	bl	List_pop_index_node
.LVL838:
	ldrh	r3, [r4, #2276]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L1015
	.loc 2 2546 0 is_stmt 0 discriminator 1
	movw	r2, #2546
	mov	r1, r9
	ldr	r0, .L1046+4
	bl	printk
.LVL839:
.L1015:
	.loc 2 2546 0 discriminator 3
	ldrh	r3, [r4, #2276]
	.loc 2 2550 0 is_stmt 1 discriminator 3
	mov	r0, r5
	.loc 2 2546 0 discriminator 3
	subs	r3, r3, #1
	strh	r3, [r4, #2276]	@ movhi
	.loc 2 2549 0 discriminator 3
	strh	r8, [r5]	@ movhi
	.loc 2 2550 0 discriminator 3
	bl	make_superblock
.LVL840:
	.loc 2 2551 0 discriminator 3
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1016
	.loc 2 2555 0
	ldr	r3, [r4, #2260]
	uxth	r6, r6
	movw	r2, #65535
	.loc 2 2556 0
	mov	r0, r8
	.loc 2 2555 0
	strh	r2, [r3, r6, lsl #1]	@ movhi
	.loc 2 2556 0
	bl	INSERT_DATA_LIST
.LVL841:
	.loc 2 2557 0
	ldrh	r2, [r4, #2268]
	ldrh	r3, [r4, #2276]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1012
	.loc 2 2557 0 is_stmt 0 discriminator 1
	movw	r2, #2557
	mov	r1, r9
	ldr	r0, .L1046+4
	bl	printk
.LVL842:
	b	.L1012
.L1038:
	movs	r1, #0
.LVL843:
	b	.L1014
.LVL844:
.L1016:
	.loc 2 2560 0 is_stmt 1
	ldrh	r2, [r4, #2268]
	ldrh	r3, [r4, #2276]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1018
	.loc 2 2560 0 is_stmt 0 discriminator 1
	mov	r2, #2560
	mov	r1, r9
	ldr	r0, .L1046+4
	bl	printk
.LVL845:
.L1018:
	.loc 2 2564 0 is_stmt 1 discriminator 1
	ldr	r1, [r4, #3760]
	mov	r10, #20
	ldrh	r2, [r4, #8]
	add	fp, r5, #16
	movs	r7, #0
	mov	r0, fp
	mov	r3, r1
	mov	ip, r7
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L1019:
	.loc 2 2563 0 discriminator 1
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L1021
	.loc 2 2571 0
	cbnz	r7, .L1022
	.loc 2 2571 0 is_stmt 0 discriminator 1
	movw	r2, #2571
	mov	r1, r9
	ldr	r0, .L1046+4
	bl	printk
.LVL846:
.L1022:
	.loc 2 2573 0 is_stmt 1
	ldrh	r3, [r4, #2568]
	cmp	r3, r8
	bne	.L1023
	.loc 2 2573 0 is_stmt 0 discriminator 1
	movw	r2, #2573
	mov	r1, r9
	ldr	r0, .L1046+4
	bl	printk
.LVL847:
.L1023:
	.loc 2 2574 0 is_stmt 1
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	.loc 2 2575 0
	ldr	r2, [r4, #2248]
	.loc 2 2574 0
	cmp	r3, #0
	bne	.L1024
	.loc 2 2575 0
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L1025
	.loc 2 2576 0
	ldrh	r1, [r4, #68]
	add	r3, r3, r1
.L1045:
	.loc 2 2578 0
	strh	r3, [r2, r6, lsl #1]	@ movhi
	.loc 2 2580 0
	movs	r1, #0
	.loc 2 2579 0
	ldr	r3, [r4, #2480]
	.loc 2 2580 0
	mov	r0, r8
	.loc 2 2579 0
	adds	r3, r3, #1
	str	r3, [r4, #2480]
	.loc 2 2580 0
	bl	ftl_set_blk_mode
.LVL848:
.L1027:
	.loc 2 2587 0
	ldr	r3, [r4, #2248]
	ldr	r2, [r4, #2496]
	.loc 2 2590 0
	ldrh	r0, [r4, #68]
	.loc 2 2587 0
	ldrh	r3, [r3, r6, lsl #1]
	.loc 2 2590 0
	ldrh	r1, [r4, #16]
	.loc 2 2587 0
	cmp	r3, r2
	.loc 2 2590 0
	ldr	r2, [r4, #2480]
	.loc 2 2588 0
	it	hi
	strhi	r3, [r4, #2496]
	.loc 2 2590 0
	ldr	r3, [r4, #2484]
	mla	r0, r2, r0, r3
	bl	__aeabi_uidiv
.LVL849:
	.loc 2 2591 0
	ldr	r2, [r4, #3820]
	.loc 2 2594 0
	ldr	r1, [r4, #3760]
	.loc 2 2590 0
	str	r0, [r4, #2488]
	.loc 2 2591 0
	ldr	r3, [r2, #16]
	.loc 2 2594 0
	ldr	r0, .L1046+16
	.loc 2 2591 0
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L1029:
	adds	r3, r3, #20
	.loc 2 2593 0 discriminator 1
	cmp	r2, r3
	bne	.L1030
	.loc 2 2595 0
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3760]
	.loc 2 2597 0
	mov	r10, #0
	.loc 2 2595 0
	bl	FlashEraseBlocks
.LVL850:
	.loc 2 2596 0
	mov	r3, r10
.LVL851:
	.loc 2 2598 0
	movs	r1, #20
.L1031:
	.loc 2 2597 0 discriminator 1
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L1033
	.loc 2 2608 0
	cmp	r3, #0
	ble	.L1034
	.loc 2 2609 0
	mov	r0, r8
	bl	update_multiplier_value
.LVL852:
	.loc 2 2610 0
	bl	FtlBbmTblFlush
.LVL853:
.L1034:
	.loc 2 2613 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1035
	.loc 2 2615 0
	ldr	r3, [r4, #2260]
.LVL854:
	movw	r2, #65535
	.loc 2 2616 0
	mov	r0, r8
	.loc 2 2615 0
	strh	r2, [r3, r6, lsl #1]	@ movhi
	.loc 2 2616 0
	bl	INSERT_DATA_LIST
.LVL855:
	.loc 2 2617 0
	b	.L1012
.L1021:
	.loc 2 2564 0
	str	ip, [r3, #8]
	.loc 2 2566 0
	movw	lr, #65535
	.loc 2 2565 0
	str	ip, [r3, #12]
	.loc 2 2566 0
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L1020
	.loc 2 2567 0
	mla	lr, r10, r7, r1
	.loc 2 2568 0
	adds	r7, r7, #1
.LVL856:
	.loc 2 2567 0
	lsls	r2, r2, #10
	.loc 2 2568 0
	uxth	r7, r7
.LVL857:
	.loc 2 2567 0
	str	r2, [lr, #4]
.L1020:
	adds	r3, r3, #20
	b	.L1019
.L1025:
	.loc 2 2578 0
	movs	r3, #2
	b	.L1045
.L1024:
	.loc 2 2582 0
	ldrh	r3, [r2, r6, lsl #1]
.LBB428:
.LBB429:
	mov	r0, r8
.LBE429:
.LBE428:
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	.loc 2 2583 0
	ldr	r3, [r4, #2484]
	adds	r3, r3, #1
	str	r3, [r4, #2484]
.LBB431:
.LBB430:
	bl	ftl_set_blk_mode.part.6
.LVL858:
	b	.L1027
.L1030:
.LBE430:
.LBE431:
	.loc 2 2594 0 discriminator 3
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1029
.LVL859:
.L1033:
	.loc 2 2598 0
	mul	r2, r1, r10
	ldr	r0, [r4, #3760]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L1032
	.loc 2 2600 0
	ldr	r0, [ip, #4]
	.loc 2 2599 0
	adds	r3, r3, #1
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	.loc 2 2601 0
	ubfx	r0, r0, #10, #16
	.loc 2 2599 0
	str	r3, [sp, #4]
	.loc 2 2601 0
	bl	FtlBbmMapBadBlock
.LVL860:
	.loc 2 2602 0
	ldr	r2, [sp, #8]
	.loc 2 2603 0
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
.LVL861:
	.loc 2 2602 0
	strh	r2, [fp]	@ movhi
	.loc 2 2603 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	subs	r2, r2, #1
	strb	r2, [r5, #7]
.L1032:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L1031
.L1035:
	.loc 2 2620 0
	ldrh	r3, [r4, #78]
.LVL862:
	.loc 2 2621 0
	strh	r8, [r5]	@ movhi
	.loc 2 2620 0
	smulbb	r3, r3, r2
	.loc 2 2622 0
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	.loc 2 2623 0
	strb	r2, [r5, #6]
	.loc 2 2624 0
	ldr	r2, [r4, #2472]
	.loc 2 2620 0
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	.loc 2 2624 0
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #2472]
	.loc 2 2625 0
	ldr	r2, [r4, #2260]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 2 2626 0
	ldrh	r3, [r5, #4]
	cbz	r3, .L1036
	.loc 2 2626 0 is_stmt 0 discriminator 2
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1037
.L1036:
	.loc 2 2626 0 discriminator 3
	movw	r2, #2626
	ldr	r1, .L1046+20
	ldr	r0, .L1046+4
	bl	printk
.LVL863:
.L1037:
	.loc 2 2628 0 is_stmt 1
	movs	r0, #0
	add	sp, sp, #20
.LCFI131:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL864:
.L1047:
	.align	2
.L1046:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+2376
	.word	.LANCHOR0+2272
	.word	-1024
	.word	.LANCHOR1+360
.LFE248:
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	1
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
.LFB267:
	.loc 5 390 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL865:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI132:
	.loc 5 390 0
	mov	r8, r0
	.loc 5 395 0
	ldr	r4, .L1060
	ldrh	r3, [r4, #3186]
	cbz	r3, .L1049
	movs	r7, #0
	addw	r10, r4, #3188
.LVL866:
.L1050:
.LBB434:
.LBB435:
	.loc 5 396 0
	ldrh	r2, [r4, #8]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1056
	.loc 5 412 0
	bl	FtlGcReFreshBadBlk
.LVL867:
.L1049:
.LBE435:
.LBE434:
	.loc 5 415 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL868:
.L1056:
.LBB437:
.LBB436:
	.loc 5 397 0
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL869:
	mov	r9, r0
.L1051:
	.loc 5 398 0
	ldrh	r3, [r4, #3186]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L1055
	adds	r7, r7, #1
	b	.L1050
.L1055:
	.loc 5 399 0
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3188]
	cmp	r3, r9
	bne	.L1052
	.loc 5 401 0
	mov	r1, r9
	ldr	r0, .L1060+4
	bl	printk
.LVL870:
	.loc 5 403 0
	mov	r0, r9
	bl	FtlBbmMapBadBlock
.LVL871:
	.loc 5 404 0
	bl	FtlBbmTblFlush
.LVL872:
	.loc 5 405 0
	ldrh	r1, [r4, #3186]
	add	r3, r10, r6, lsl #1
.L1053:
	cmp	r5, r1
	bcc	.L1054
	.loc 5 407 0
	subs	r1, r1, #1
	strh	r1, [r4, #3186]	@ movhi
.L1052:
	add	fp, fp, #1
	b	.L1051
.L1054:
	.loc 5 406 0
	ldrh	r0, [r3, #2]!
	.loc 5 405 0
	adds	r5, r5, #1
.LVL873:
	uxth	r5, r5
.LVL874:
	.loc 5 406 0
	strh	r0, [r3, #-2]	@ movhi
	b	.L1053
.L1061:
	.align	2
.L1060:
	.word	.LANCHOR0
	.word	.LC93
.LBE436:
.LBE437:
.LFE267:
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	1
	.global	update_vpc_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
.LFB251:
	.loc 2 2687 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL875:
	.loc 2 2688 0
	ldr	r2, .L1072
	.loc 2 2687 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI133:
	.loc 2 2687 0
	mov	r5, r0
	mov	r4, r2
	.loc 2 2688 0
	ldr	r3, [r2, #2260]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L1063
	.loc 2 2689 0
	ldrh	r1, [r2, #2568]
	cmp	r1, r0
	bne	.L1064
	.loc 2 2691 0
	movw	r3, #65535
	strh	r3, [r2, #2568]	@ movhi
.L1065:
.LVL876:
.LBB440:
.LBB441:
	.loc 2 2696 0
	mov	r1, r5
	ldr	r0, .L1072+4
	bl	List_remove_node
.LVL877:
	ldrh	r3, [r4, #2268]
	cbnz	r3, .L1067
	movw	r2, #2696
	ldr	r1, .L1072+8
	ldr	r0, .L1072+12
	bl	printk
.LVL878:
.L1067:
	ldrh	r3, [r4, #2268]
	.loc 2 2697 0
	mov	r0, r5
	.loc 2 2696 0
	subs	r3, r3, #1
	strh	r3, [r4, #2268]	@ movhi
	.loc 2 2697 0
	bl	free_data_superblock
.LVL879:
	.loc 2 2698 0
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
.LVL880:
	.loc 2 2699 0
	ldrh	r2, [r4, #2268]
	ldrh	r3, [r4, #2276]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1071
	movw	r2, #2699
	ldr	r1, .L1072+8
	ldr	r0, .L1072+12
	bl	printk
.LVL881:
.L1071:
	movs	r3, #1
	b	.L1062
.LVL882:
.L1064:
.LBE441:
.LBE440:
	.loc 2 2692 0
	ldrh	r1, [r2, #2280]
	cmp	r1, r0
	beq	.L1062
	.loc 2 2692 0 is_stmt 0 discriminator 1
	ldrh	r1, [r2, #2328]
	cmp	r1, r0
	beq	.L1062
	.loc 2 2692 0 discriminator 2
	ldrh	r2, [r2, #2376]
	cmp	r2, r0
	bne	.L1065
.L1062:
	.loc 2 2705 0 is_stmt 1
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.LVL883:
.L1063:
	.loc 2 2702 0
	bl	List_update_data_list
.LVL884:
	.loc 2 2704 0
	movs	r3, #0
	b	.L1062
.L1073:
	.align	2
.L1072:
	.word	.LANCHOR0
	.word	.LANCHOR0+2256
	.word	.LANCHOR1+385
	.word	.LC1
.LFE251:
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	1
	.global	decrement_vpc_count
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
.LFB252:
	.loc 2 2708 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL885:
	.loc 2 2711 0
	movw	r3, #65535
	.loc 2 2708 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI134:
	.loc 2 2711 0
	cmp	r0, r3
	.loc 2 2708 0
	mov	r4, r0
	ldr	r5, .L1083
	.loc 2 2711 0
	beq	.L1075
	.loc 2 2712 0
	ldr	r3, [r5, #2260]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1076
	.loc 2 2713 0
	mov	r1, r0
	ldr	r0, .L1083+4
	bl	printk
.LVL886:
	.loc 2 2714 0
	ldr	r3, [r5, #2260]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L1077
.L1081:
	.loc 2 2723 0
	movs	r6, #0
.L1074:
	.loc 2 2733 0
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.LVL887:
.L1077:
	.loc 2 2714 0 discriminator 1
	movw	r2, #2714
.L1082:
	.loc 2 2730 0 discriminator 2
	ldr	r1, .L1083+8
	ldr	r0, .L1083+12
	bl	printk
.LVL888:
	b	.L1074
.LVL889:
.L1076:
	.loc 2 2717 0
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L1075:
	.loc 2 2721 0
	ldrh	r0, [r5, #3952]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1079
	.loc 2 2722 0
	strh	r4, [r5, #3952]	@ movhi
	b	.L1081
.L1079:
	.loc 2 2724 0
	cmp	r4, r0
	beq	.L1081
	.loc 2 2727 0
	bl	update_vpc_list
.LVL890:
	.loc 2 2730 0
	ldr	r2, [r5, #2252]
	.loc 2 2727 0
	adds	r6, r0, #0
	.loc 2 2730 0
	ldr	r3, [r5, #2256]
	.loc 2 2727 0
	it	ne
	movne	r6, #1
	.loc 2 2729 0
	strh	r4, [r5, #3952]	@ movhi
	.loc 2 2730 0
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L1083+16
	muls	r3, r2, r3
	ldr	r2, [r5, #2260]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L1074
	.loc 2 2730 0 is_stmt 0 discriminator 1
	cmp	r4, r1
	beq	.L1074
	.loc 2 2730 0 discriminator 2
	movw	r2, #2730
	b	.L1082
.L1084:
	.align	2
.L1083:
	.word	.LANCHOR0
	.word	.LC94
	.word	.LANCHOR1+401
	.word	.LC1
	.word	-1431655765
.LFE252:
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	1
	.global	FtlWriteDump_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	.fnstart
.LFB218:
	.loc 2 947 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI135:
	.pad #24
	sub	sp, sp, #24
.LCFI136:
	.loc 2 948 0
	ldr	r4, .L1103
	ldrh	r2, [r4, #2284]
	cmp	r2, #0
	beq	.L1086
	.loc 2 948 0 is_stmt 0 discriminator 1
	ldrb	r3, [r4, #2288]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1086
	.loc 2 949 0 is_stmt 1
	ldrb	r1, [r4, #2287]	@ zero_extendqisi2
	ldrh	r3, [r4, #78]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1086
.LBB445:
.LBB446:
	.loc 2 956 0
	ldrb	r7, [r4, #2290]	@ zero_extendqisi2
	cbnz	r7, .L1085
	.loc 2 951 0
	ldr	r6, [r4, #2436]
	.loc 2 958 0
	mov	r2, r7
	mov	r1, sp
	.loc 2 953 0
	ldrh	r8, [r4, #8]
	.loc 2 951 0
	subs	r6, r6, #1
.LVL891:
	.loc 2 958 0
	mov	r0, r6
	bl	log2phys
.LVL892:
	.loc 2 961 0
	ldr	r3, [sp]
	.loc 2 964 0
	ldr	r5, [r4, #3244]
	.loc 2 963 0
	ldr	r0, [r4, #2244]
	.loc 2 961 0
	str	r3, [sp, #8]
	.loc 2 967 0
	adds	r3, r3, #1
	.loc 2 962 0
	str	r6, [sp, #20]
	.loc 2 963 0
	str	r0, [sp, #12]
	.loc 2 964 0
	str	r5, [sp, #16]
	.loc 2 966 0
	str	r7, [r5, #4]
	.loc 2 967 0
	beq	.L1088
	.loc 2 969 0
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.LVL893:
.L1089:
	.loc 2 986 0
	ldr	r9, .L1103+4
	.loc 2 975 0
	movs	r7, #0
	.loc 2 953 0
	lsl	r8, r8, #2
.LVL894:
	.loc 2 987 0
	mov	r10, r7
	.loc 2 975 0
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1090:
	.loc 2 976 0
	cmp	r8, r7
	bne	.L1094
.L1091:
	.loc 2 992 0
	movs	r3, #1
.L1102:
.LBE446:
.LBE445:
	.loc 2 998 0
	strb	r3, [r4, #2290]
.L1085:
	.loc 2 1000 0
	add	sp, sp, #24
.LCFI137:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL895:
.L1088:
.LCFI138:
.LBB448:
.LBB447:
	.loc 2 973 0
	ldrh	r2, [r4, #86]
	movs	r1, #255
	bl	ftl_memset
.LVL896:
	b	.L1089
.LVL897:
.L1094:
	.loc 2 978 0
	ldrh	r3, [r4, #2284]
	cmp	r3, #0
	beq	.L1091
	.loc 2 983 0
	ldr	r3, [sp, #8]
	.loc 2 986 0
	mov	r0, r9
	.loc 2 982 0
	str	r6, [r5, #8]
	adds	r7, r7, #1
	.loc 2 983 0
	str	r3, [r5, #12]
	.loc 2 984 0
	ldrh	r3, [r4, #2280]
	strh	r3, [r5, #2]	@ movhi
	.loc 2 986 0
	bl	get_new_active_ppa
.LVL898:
	.loc 2 987 0
	ldr	r3, [r4, #2476]
	.loc 2 988 0
	movs	r1, #1
	.loc 2 986 0
	str	r0, [sp, #8]
	.loc 2 988 0
	add	r0, sp, #4
	.loc 2 987 0
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r10
	str	r3, [r4, #2476]
	.loc 2 988 0
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL899:
	.loc 2 990 0
	ldrh	r0, [r4, #2280]
	bl	decrement_vpc_count
.LVL900:
	b	.L1090
.LVL901:
.L1086:
.LBE447:
.LBE448:
	.loc 2 998 0
	movs	r3, #0
	b	.L1102
.L1104:
	.align	2
.L1103:
	.word	.LANCHOR0
	.word	.LANCHOR0+2280
.LFE218:
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	1
	.global	l2p_flush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
.LFB211:
	.loc 2 740 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI139:
	.loc 2 746 0
	movs	r4, #0
	ldr	r5, .L1109
	.loc 2 747 0
	movs	r6, #12
	.loc 2 744 0
	bl	FtlWriteDump_data
.LVL902:
.L1106:
	.loc 2 746 0 discriminator 1
	ldrh	r3, [r5, #114]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1108
	.loc 2 750 0
	movs	r0, #0
.LVL903:
	pop	{r4, r5, r6, pc}
.LVL904:
.L1108:
	.loc 2 747 0
	ldr	r2, [r5, #2424]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1107
	.loc 2 748 0
	bl	flush_l2p_region
.LVL905:
.L1107:
	adds	r4, r4, #1
	b	.L1106
.L1110:
	.align	2
.L1109:
	.word	.LANCHOR0
.LFE211:
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	1
	.global	FtlRecoverySuperblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
.LFB231:
	.loc 2 1656 0
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL906:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI140:
	.loc 2 1670 0
	movw	r2, #65535
	ldrh	r3, [r0]
	.loc 2 1656 0
	.pad #52
	sub	sp, sp, #52
.LCFI141:
	.loc 2 1656 0
	mov	fp, r0
	.loc 2 1670 0
	cmp	r3, r2
	beq	.L1236
	.loc 2 1675 0
	ldrh	r3, [r0, #2]
.LVL907:
	.loc 2 1681 0
	ldr	r4, .L1243
	.loc 2 1675 0
	str	r3, [sp, #8]
	.loc 2 1676 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
.LVL908:
	.loc 2 1681 0
	ldr	r1, [sp, #8]
.LVL909:
	.loc 2 1676 0
	str	r3, [sp, #24]
	.loc 2 1681 0
	ldrh	r3, [r4, #78]
.LVL910:
	cmp	r3, r1
	mov	r3, #0
	bne	.L1114
	.loc 2 1682 0
	strh	r3, [r0, #4]	@ movhi
.LVL911:
.L1241:
	.loc 2 1989 0
	strb	r3, [fp, #6]
.L1236:
	.loc 2 2010 0
	movs	r0, #0
	add	sp, sp, #52
.LCFI142:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL912:
.L1114:
.LCFI143:
	.loc 2 1689 0
	ldrh	r0, [r0, #16]
.LVL913:
.L1115:
	.loc 2 1690 0
	cmp	r0, r2
	uxth	r5, r3
	add	r3, r3, #1
	beq	.L1116
	.loc 2 1697 0
	movs	r1, #1
.LVL914:
	bl	FtlGetLastWrittenPage
.LVL915:
	.loc 2 1705 0
	adds	r7, r0, #1
	.loc 2 1697 0
	mov	r6, r0
.LVL916:
	.loc 2 1705 0
	beq	.L1117
	.loc 2 1721 0
	ldr	r3, [r4, #3152]
	.loc 2 1722 0
	movs	r2, #0
	.loc 2 1717 0
	ldrh	lr, [r4, #8]
	.loc 2 1719 0
	movw	r9, #65535
	.loc 2 1720 0
	ldr	r0, [r4, #3792]
	mov	r10, #20
	.loc 2 1721 0
	str	r3, [sp, #4]
	ldrh	r3, [r4, #86]
	.loc 2 1722 0
	ldr	r8, [r4, #3156]
	ldrh	r7, [r4, #88]
	mov	r4, r2
	.loc 2 1721 0
	str	r3, [sp, #12]
	add	r3, fp, #16
	.loc 2 1722 0
	mov	ip, r3
	str	r3, [sp, #20]
.L1118:
	.loc 2 1717 0 discriminator 1
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L1124
	.loc 2 1728 0
	ldr	r5, .L1243
	.loc 2 1727 0
	movs	r2, #0
	mov	r1, r4
	.loc 2 1666 0
	movw	r9, #65535
	.loc 2 1727 0
	bl	FlashReadPages
.LVL917:
	.loc 2 1728 0
	ldr	r3, [r5, #2476]
	.loc 2 1742 0
	uxth	r2, r6
	.loc 2 1730 0
	ldr	r8, [r5, #3792]
	.loc 2 1742 0
	str	r2, [sp, #12]
	.loc 2 1728 0
	subs	r3, r3, #1
.LVL918:
	str	r3, [sp, #4]
	mov	r7, r8
	.loc 2 1729 0
	movs	r3, #0
.LVL919:
.L1125:
	uxth	r2, r3
.LVL920:
	.loc 2 1729 0 is_stmt 0 discriminator 1
	cmp	r4, r2
	bhi	.L1130
	.loc 2 1745 0 is_stmt 1
	bne	.L1128
	.loc 2 1747 0
	adds	r3, r6, #1
	uxth	r3, r3
.LVL921:
	str	r3, [sp, #16]
.L1237:
	.loc 2 1753 0
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
.LVL922:
	.loc 2 1758 0
	ldrh	r3, [r5, #78]
	ldr	r2, [sp, #16]
.LVL923:
	.loc 2 1753 0
	str	r0, [sp, #12]
	.loc 2 1758 0
	cmp	r3, r2
	bne	.L1132
	.loc 2 1759 0
	ldrh	r3, [sp, #16]
	strh	r3, [fp, #2]	@ movhi
	.loc 2 1760 0
	movs	r3, #0
	strb	r3, [fp, #6]
	.loc 2 1761 0
	strh	r3, [fp, #4]	@ movhi
.L1132:
	.loc 2 1764 0
	ldr	r3, [sp, #16]
.LVL924:
	ldr	r2, [sp, #8]
.LVL925:
	cmp	r3, r2
	bne	.L1133
	.loc 2 1764 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #12]
.LVL926:
	ldr	r2, [sp, #24]
.LVL927:
	cmp	r3, r2
	bne	.L1133
	.loc 2 1765 0 is_stmt 1
	ldr	r1, [sp, #16]
	mov	r2, r3
.LVL928:
.L1242:
	.loc 2 2004 0
	mov	r0, fp
.LVL929:
	bl	ftl_sb_update_avl_pages
.LVL930:
	b	.L1236
.LVL931:
.L1116:
	.loc 2 1692 0
	uxth	r1, r3
.LVL932:
	adds	r1, r1, #8
	ldrh	r0, [fp, r1, lsl #1]
	b	.L1115
.LVL933:
.L1117:
	.loc 2 1706 0
	ldr	r3, [sp, #8]
.LVL934:
	cbz	r3, .L1119
	.loc 2 1706 0 is_stmt 0 discriminator 1
	movw	r2, #1706
	ldr	r1, .L1243+4
	ldr	r0, .L1243+8
	bl	printk
.LVL935:
.L1119:
	.loc 2 1707 0 is_stmt 1
	ldr	r3, [sp, #24]
.LVL936:
	cbz	r3, .L1120
	.loc 2 1707 0 is_stmt 0 discriminator 1
	cmp	r5, r3
	beq	.L1120
	.loc 2 1707 0 discriminator 2
	movw	r2, #1707
	ldr	r1, .L1243+4
	ldr	r0, .L1243+8
	bl	printk
.LVL937:
.L1120:
	.loc 2 1708 0 is_stmt 1
	movs	r3, #0
.LVL938:
	strh	r3, [fp, #2]	@ movhi
	b	.L1241
.L1124:
	.loc 2 1718 0
	ldrh	r3, [ip], #2
	.loc 2 1719 0
	cmp	r3, r9
	beq	.L1121
	.loc 2 1720 0
	mla	r1, r10, r4, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	.loc 2 1721 0
	ldr	r3, [sp, #12]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r5, r3
	ldr	r3, [sp, #4]
	add	r5, r5, r3
	.loc 2 1722 0
	mov	r3, r7
	muls	r3, r4, r3
	.loc 2 1723 0
	add	r4, r4, #1
	.loc 2 1722 0
	it	mi
	addmi	r3, r3, #3
	.loc 2 1723 0
	uxth	r4, r4
	.loc 2 1722 0
	bic	r3, r3, #3
	.loc 2 1721 0
	str	r5, [r1, #8]
	.loc 2 1722 0
	add	r3, r3, r8
	str	r3, [r1, #12]
.L1121:
	adds	r2, r2, #1
	b	.L1118
.LVL939:
.L1130:
	.loc 2 1730 0
	ldr	r2, [r7]
.LVL940:
	cbnz	r2, .L1126
	.loc 2 1731 0
	ldr	r10, [r7, #12]
	.loc 2 1732 0
	ldr	r2, [r10, #4]
	adds	r0, r2, #1
	beq	.L1127
	.loc 2 1733 0
	ldr	r1, [r5, #2476]
	mov	r0, r2
	bl	ftl_cmp_data_ver
.LVL941:
	cbz	r0, .L1127
	.loc 2 1735 0
	adds	r2, r2, #1
	str	r2, [r5, #2476]
.L1127:
	.loc 2 1738 0
	ldr	r2, [r10]
	adds	r2, r2, #1
	bne	.L1129
.LVL942:
.L1128:
	.loc 2 1752 0
	uxth	r2, r6
.LVL943:
	.loc 2 1753 0
	uxth	r3, r3
	.loc 2 1752 0
	str	r2, [sp, #16]
	.loc 2 1753 0
	movs	r2, #20
.LVL944:
	mla	r8, r2, r3, r8
	b	.L1237
.L1126:
	.loc 2 1742 0
	ldr	r9, [sp, #12]
.L1129:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1125
.LVL945:
.L1133:
	.loc 2 1770 0
	movw	r3, #65535
.LVL946:
	cmp	r9, r3
	bne	.L1134
	.loc 2 1770 0 is_stmt 0 discriminator 1
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1135
.L1134:
.LBB449:
	.loc 2 1775 0 is_stmt 1
	ldr	r3, [r5, #3964]
	.loc 2 1774 0
	uxth	r8, r6
	.loc 2 1778 0
	uxth	r6, r6
.LVL947:
	ldr	r7, .L1243
	.loc 2 1784 0
	movw	r9, #65535
	.loc 2 1775 0
	adds	r3, r3, #1
	.loc 2 1776 0
	itt	eq
	ldreq	r3, [sp, #4]
.LVL948:
	streq	r3, [r5, #3964]
	.loc 2 1778 0
	ldr	r3, [sp, #8]
.LVL949:
	adds	r3, r3, #7
.LVL950:
	cmp	r6, r3
	.loc 2 1779 0
	itet	gt
	subgt	r4, r8, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L1138:
	.loc 2 1780 0 discriminator 1
	cmp	r4, r8
	bhi	.L1145
	.loc 2 1785 0
	movs	r3, #0
	.loc 2 1782 0
	ldrh	lr, [r7, #8]
	.loc 2 1785 0
	ldr	r0, [r7, #3792]
	mov	r6, r3
	ldr	r1, [sp, #20]
	mov	ip, #20
	b	.L1146
.LVL951:
.L1140:
	.loc 2 1783 0
	ldrh	r2, [r1], #2
	.loc 2 1784 0
	cmp	r2, r9
	beq	.L1139
	.loc 2 1785 0
	mla	r10, ip, r6, r0
	.loc 2 1786 0
	adds	r6, r6, #1
	.loc 2 1785 0
	orr	r2, r4, r2, lsl #10
	.loc 2 1786 0
	uxth	r6, r6
	.loc 2 1785 0
	str	r2, [r10, #4]
.L1139:
	adds	r3, r3, #1
.LVL952:
.L1146:
	.loc 2 1782 0 discriminator 1
	uxth	r2, r3
.LVL953:
	cmp	r2, lr
	bcc	.L1140
	.loc 2 1789 0
	movs	r2, #0
	mov	r1, r6
	bl	FlashReadPages
.LVL954:
	.loc 2 1791 0
	ldr	r3, [r7, #3792]
	movs	r2, #20
	mla	r6, r2, r6, r3
.L1141:
	.loc 2 1790 0 discriminator 1
	cmp	r6, r3
	bne	.L1144
	.loc 2 1780 0
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1138
.L1144:
	.loc 2 1791 0
	ldr	r2, [r3]
	cbnz	r2, .L1135
	.loc 2 1792 0
	ldr	r2, [r3, #12]
	.loc 2 1793 0
	ldrh	r1, [r2]
	cmp	r1, r9
	beq	.L1143
	.loc 2 1795 0
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	.loc 2 1797 0
	it	ne
	strne	r2, [r7, #3964]
.L1143:
	adds	r3, r3, #20
	b	.L1141
.LVL955:
.L1145:
	.loc 2 1809 0
	mov	r3, #-1
	str	r3, [r7, #3964]
.LVL956:
.L1135:
.LBE449:
	.loc 2 1814 0
	movs	r3, #1
	.loc 2 1815 0
	ldr	r0, .L1243+12
	.loc 2 1814 0
	strh	r3, [r5, #3968]	@ movhi
	.loc 2 1821 0
	ldr	r4, .L1243
	.loc 2 1815 0
	bl	FtlMapBlkWriteDump_data
.LVL957:
	ldr	r5, [sp, #8]
.LVL958:
.L1147:
	.loc 2 1821 0
	ldrh	r6, [r4, #8]
.LVL959:
	movs	r3, #0
	.loc 2 1824 0
	ldr	r0, [r4, #3792]
.LVL960:
	.loc 2 1820 0
	mov	r10, r3
	.loc 2 1821 0
	ldr	r1, [sp, #20]
	.loc 2 1823 0
	movw	r7, #65535
	.loc 2 1824 0
	mov	ip, #20
.L1148:
	.loc 2 1821 0 discriminator 1
	uxth	r2, r3
.LVL961:
	cmp	r6, r2
	bhi	.L1150
	.loc 2 1828 0
	movs	r2, #0
	mov	r1, r10
	bl	FlashReadPages
.LVL962:
	.loc 2 1829 0
	movs	r3, #0
.LVL963:
.L1240:
	str	r3, [sp, #28]
	.loc 2 1829 0 is_stmt 0 discriminator 2
	ldrh	r3, [sp, #28]
	cmp	r10, r3
	bhi	.L1180
	.loc 2 1982 0 is_stmt 1
	adds	r5, r5, #1
	.loc 2 1983 0
	ldrh	r3, [r4, #78]
	.loc 2 1982 0
	uxth	r5, r5
	.loc 2 1983 0
	cmp	r3, r5
	bne	.L1147
	.loc 2 1986 0
	ldrh	r2, [r4, #8]
.LVL964:
	.loc 2 1988 0
	movw	r0, #65535
.LVL965:
	.loc 2 1985 0
	movs	r3, #0
	.loc 2 1984 0
	strh	r5, [fp, #2]	@ movhi
	.loc 2 1985 0
	strh	r3, [fp, #4]	@ movhi
.L1181:
	.loc 2 1986 0 discriminator 1
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1236
	.loc 2 1987 0
	ldr	r1, [sp, #20]
	ldrh	r4, [r1], #2
	.loc 2 1988 0
	cmp	r4, r0
	.loc 2 1987 0
	str	r1, [sp, #20]
	add	r1, r3, #1
	.loc 2 1988 0
	bne	.L1241
	mov	r3, r1
	b	.L1181
.LVL966:
.L1150:
	.loc 2 1822 0
	ldrh	r2, [r1], #2
	.loc 2 1823 0
	cmp	r2, r7
	beq	.L1149
	.loc 2 1824 0
	mla	lr, ip, r10, r0
	.loc 2 1825 0
	add	r10, r10, #1
	.loc 2 1824 0
	orr	r2, r5, r2, lsl #10
	.loc 2 1825 0
	uxth	r10, r10
	.loc 2 1824 0
	str	r2, [lr, #4]
.L1149:
	adds	r3, r3, #1
	b	.L1148
.L1244:
	.align	2
.L1243:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3864
.LVL967:
.L1180:
	ldr	r3, [sp, #28]
	movs	r7, #20
	.loc 2 1830 0
	ldr	r9, [r4, #3792]
.LVL968:
	muls	r7, r3, r7
	add	r8, r9, r7
	ldr	r6, [r8, #4]
.LVL969:
	.loc 2 1831 0
	ubfx	r0, r6, #10, #16
.LVL970:
	.loc 2 1830 0
	str	r6, [sp, #44]
	.loc 2 1831 0
	bl	P2V_plane
.LVL971:
	.loc 2 1833 0
	ldr	r3, [sp, #8]
.LVL972:
	cmp	r5, r3
	bcc	.L1152
	.loc 2 1833 0 is_stmt 0 discriminator 1
	bne	.L1153
	.loc 2 1834 0 is_stmt 1
	ldr	r3, [sp, #24]
.LVL973:
	cmp	r3, r0
	bhi	.L1152
.LVL974:
.L1153:
	.loc 2 1837 0
	ldr	r3, [sp, #16]
.LVL975:
	cmp	r5, r3
	bne	.L1154
	.loc 2 1837 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #12]
.LVL976:
	cmp	r3, r0
	beq	.L1155
.LVL977:
.L1154:
	.loc 2 1841 0 is_stmt 1
	ldr	r3, [r9, r7]
.LVL978:
	adds	r3, r3, #1
	beq	.L1156
	.loc 2 1842 0
	ldr	r8, [r8, #12]
	.loc 2 1843 0
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1157
.LVL979:
.L1164:
.LBB450:
	.loc 2 1876 0
	ldrh	r0, [fp]
.LVL980:
.L1239:
.LBE450:
	.loc 2 1960 0
	bl	decrement_vpc_count
.LVL981:
.L1152:
	ldr	r3, [sp, #28]
.LVL982:
	adds	r3, r3, #1
	b	.L1240
.LVL983:
.L1157:
	.loc 2 1849 0
	ldr	r3, [r8, #4]
.LVL984:
	.loc 2 1850 0
	adds	r6, r3, #1
	.loc 2 1849 0
	str	r3, [sp, #4]
	.loc 2 1850 0
	beq	.L1158
	.loc 2 1851 0 discriminator 1
	ldr	r1, [r4, #2476]
	mov	r0, r3
.LVL985:
	bl	ftl_cmp_data_ver
.LVL986:
	.loc 2 1850 0 discriminator 1
	cbz	r0, .L1158
	.loc 2 1853 0
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
.LVL987:
	str	r3, [r4, #2476]
.LVL988:
.L1158:
	.loc 2 1856 0
	ldrh	r2, [r8]
	movw	r3, #61589
.LVL989:
	cmp	r2, r3
	beq	.L1159
	.loc 2 1856 0 is_stmt 0 discriminator 1
	mov	r2, #1856
	ldr	r1, .L1245
	ldr	r0, .L1245+4
.LVL990:
	bl	printk
.LVL991:
.L1159:
	.loc 2 1857 0 is_stmt 1
	ldr	r6, [r8, #8]
.LVL992:
	.loc 2 1859 0
	add	r1, sp, #40
	.loc 2 1858 0
	ldr	r3, [r8, #12]
	.loc 2 1859 0
	movs	r2, #0
	mov	r0, r6
.LVL993:
	.loc 2 1858 0
	str	r3, [sp, #36]
	.loc 2 1859 0
	bl	log2phys
.LVL994:
	.loc 2 1860 0
	ldr	r1, [r4, #3964]
	adds	r0, r1, #1
	beq	.L1160
	.loc 2 1861 0 discriminator 1
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
.LVL995:
	.loc 2 1860 0 discriminator 1
	cmp	r0, #0
	beq	.L1160
.LBB451:
	.loc 2 1865 0
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1161
	.loc 2 1866 0
	ldr	r0, [r4, #3792]
	.loc 2 1868 0
	movs	r2, #0
	movs	r1, #1
	.loc 2 1866 0
	add	r0, r0, r7
	.loc 2 1867 0
	str	r3, [r0, #4]
	.loc 2 1866 0
	ldr	r8, [r0, #12]
	.loc 2 1868 0
	bl	FlashReadPages
.LVL996:
	.loc 2 1881 0
	ldr	r2, [r4, #3792]
	ldr	r1, [r2, r7]
	adds	r3, r2, r7
	adds	r1, r1, #1
	bne	.L1162
.LVL997:
.L1163:
	.loc 2 1913 0
	mov	r3, #-1
.LVL998:
	.loc 2 1914 0
	ldrh	r0, [fp]
	.loc 2 1913 0
	str	r3, [sp, #36]
	.loc 2 1914 0
	bl	decrement_vpc_count
.LVL999:
.L1171:
.LBE451:
	.loc 2 1954 0
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1152
.L1184:
	.loc 2 1956 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1000:
	.loc 2 1957 0
	ldrh	r3, [r4, #16]
	.loc 2 1956 0
	mov	r6, r0
.LVL1001:
	.loc 2 1957 0
	cmp	r3, r0
	bhi	.L1176
	.loc 2 1957 0 is_stmt 0 discriminator 1
	movw	r2, #1957
	ldr	r1, .L1245
	ldr	r0, .L1245+4
	bl	printk
.LVL1002:
.L1176:
	.loc 2 1958 0 is_stmt 1
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1177
	.loc 2 1960 0
	mov	r0, r6
	b	.L1239
.LVL1003:
.L1161:
.LBB452:
	.loc 2 1872 0
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1164
	.loc 2 1873 0
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
.LVL1004:
	b	.L1164
.L1162:
	.loc 2 1881 0 discriminator 1
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1163
	.loc 2 1869 0 discriminator 2
	ldr	r9, [r8, #4]
.LVL1005:
	.loc 2 1882 0 discriminator 2
	ldr	r0, [r4, #3964]
	mov	r1, r9
	bl	ftl_cmp_data_ver
.LVL1006:
	.loc 2 1881 0 discriminator 2
	cmp	r0, #0
	beq	.L1163
	.loc 2 1883 0
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1166
.LVL1007:
.L1169:
	.loc 2 1909 0
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
.LVL1008:
	b	.L1163
.LVL1009:
.L1166:
	.loc 2 1886 0
	ldr	r0, [sp, #36]
.LVL1010:
	cmp	r1, r0
	beq	.L1163
	.loc 2 1887 0
	adds	r0, r1, #1
	beq	.L1167
	.loc 2 1889 0
	str	r1, [r3, #4]
	.loc 2 1890 0
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	.loc 2 1888 0
	ldr	r8, [r3, #12]
	.loc 2 1890 0
	bl	FlashReadPages
.LVL1011:
.L1168:
	.loc 2 1895 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1169
	.loc 2 1896 0
	ldr	r3, [r8, #4]
.LVL1012:
	.loc 2 1897 0
	ldr	r0, [r4, #3964]
	mov	r1, r3
	bl	ftl_cmp_data_ver
.LVL1013:
	cmp	r0, #0
	beq	.L1169
	.loc 2 1898 0
	mov	r1, r3
	mov	r0, r9
	bl	ftl_cmp_data_ver
.LVL1014:
	cmp	r0, #0
	beq	.L1163
	b	.L1169
.LVL1015:
.L1167:
	.loc 2 1892 0
	str	r1, [r2, r7]
	b	.L1168
.LVL1016:
.L1160:
.LBE452:
	.loc 2 1915 0
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1171
	.loc 2 1917 0
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
.LVL1017:
	.loc 2 1918 0
	ldr	r8, [sp, #40]
	cmp	r8, #-1
	beq	.L1171
	.loc 2 1918 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L1174
	.loc 2 1919 0 is_stmt 1
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
.LVL1018:
	.loc 2 1921 0
	ldrh	r3, [r4, #2280]
	cmp	r3, r0
	beq	.L1175
	.loc 2 1921 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2328]
	cmp	r3, r0
	beq	.L1175
	.loc 2 1922 0 is_stmt 1
	ldrh	r3, [r4, #2376]
	cmp	r3, r0
	bne	.L1171
.L1175:
.LBB453:
	.loc 2 1927 0
	ldr	r0, [r4, #3792]
	.loc 2 1930 0
	movs	r2, #0
	movs	r1, #1
	.loc 2 1929 0
	str	r8, [r0, #4]
	.loc 2 1927 0
	ldr	r7, [r0, #12]
	.loc 2 1930 0
	bl	FlashReadPages
.LVL1019:
	.loc 2 1933 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1171
	.loc 2 1934 0
	ldr	r1, [r7, #4]
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
.LVL1020:
	cmp	r0, #0
	bne	.L1171
	.loc 2 1941 0
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
.LVL1021:
	b	.L1171
.LVL1022:
.L1177:
.LBE453:
	.loc 2 1962 0
	mov	r1, r6
	ldr	r0, .L1245+8
	bl	printk
.LVL1023:
	b	.L1152
.LVL1024:
.L1156:
	.loc 2 1969 0
	ldr	r3, [r4, #3972]
	cmp	r3, #31
	.loc 2 1970 0
	itttt	ls
	addls	r2, r4, r3, lsl #2
	.loc 2 1971 0
	addls	r3, r3, #1
	strls	r3, [r4, #3972]
	.loc 2 1970 0
	strls	r6, [r2, #3976]
	.loc 2 1973 0
	ldrh	r0, [fp]
.LVL1025:
	bl	decrement_vpc_count
.LVL1026:
	.loc 2 1975 0
	ldr	r3, [r4, #3964]
	adds	r2, r3, #1
	bne	.L1179
	.loc 2 1976 0
	ldr	r3, [sp, #4]
.LVL1027:
.L1238:
	.loc 2 1978 0
	str	r3, [r4, #3964]
	b	.L1152
.LVL1028:
.L1179:
	.loc 2 1977 0
	ldr	r2, [sp, #4]
.LVL1029:
	cmp	r2, r3
	bcs	.L1152
	.loc 2 1978 0
	mov	r3, r2
.LVL1030:
	b	.L1238
.LVL1031:
.L1155:
	.loc 2 2002 0
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
.LVL1032:
	.loc 2 2004 0
	mov	r1, r5
	.loc 2 2003 0
	strh	r5, [fp, #2]	@ movhi
	.loc 2 2004 0
	ldr	r2, [sp, #12]
	.loc 2 2002 0
	strb	r3, [fp, #6]
	b	.L1242
.LVL1033:
.L1174:
	.loc 2 1954 0
	mov	r0, r8
	b	.L1184
.L1246:
	.align	2
.L1245:
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LC95
.LFE231:
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	1
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
.LFB238:
	.loc 2 2156 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1034:
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI144:
	mov	r4, r0
	ldr	r5, .L1258
	.pad #24
	sub	sp, sp, #24
.LCFI145:
	.loc 2 2162 0
	movs	r6, #7
	.loc 2 2168 0
	mov	r7, #-1
	.loc 2 2176 0
	mov	r8, #0
.L1248:
	.loc 2 2162 0
	subs	r6, r6, #1
	beq	.L1250
	.loc 2 2163 0
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1249
.L1250:
	.loc 2 2183 0
	ldr	r2, [r5, #2260]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 2 2184 0
	ldrh	r3, [r5, #78]
	strh	r3, [r4, #2]	@ movhi
	.loc 2 2185 0
	movs	r3, #0
	strb	r3, [r4, #6]
	.loc 2 2186 0
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2188 0
	add	sp, sp, #24
.LCFI146:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1035:
.L1249:
.LCFI147:
	.loc 2 2165 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1036:
	str	r0, [sp, #8]
	.loc 2 2166 0
	adds	r0, r0, #1
	beq	.L1250
	.loc 2 2170 0
	ldr	r2, [r5, #3244]
	.loc 2 2179 0
	movs	r1, #1
	.loc 2 2169 0
	ldr	r3, [r5, #2244]
	.loc 2 2179 0
	add	r0, sp, #4
	.loc 2 2168 0
	str	r7, [sp, #20]
	.loc 2 2170 0
	str	r2, [sp, #16]
	.loc 2 2169 0
	str	r3, [sp, #12]
	.loc 2 2174 0
	str	r7, [r2, #12]
	.loc 2 2173 0
	str	r7, [r2, #8]
	.loc 2 2175 0
	ldrh	r3, [r4]
	.loc 2 2176 0
	strh	r8, [r2]	@ movhi
	.loc 2 2175 0
	strh	r3, [r2, #2]	@ movhi
	.loc 2 2178 0
	ldr	r3, [r5, #2476]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r8
	str	r3, [r5, #2476]
	.loc 2 2179 0
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL1037:
	.loc 2 2180 0
	ldrh	r0, [r4]
	bl	decrement_vpc_count
.LVL1038:
	b	.L1248
.L1259:
	.align	2
.L1258:
	.word	.LANCHOR0
.LFE238:
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	1
	.global	FtlLoadBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
.LFB179:
	.loc 4 173 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI148:
	.loc 4 196 0
	movw	r8, #61649
	.loc 4 180 0
	ldr	r4, .L1289
	ldr	r3, [r4, #2244]
	.loc 4 186 0
	addw	r7, r4, #3224
	.loc 4 181 0
	ldr	r6, [r4, #3244]
	.loc 4 180 0
	str	r3, [r4, #3232]
	.loc 4 181 0
	str	r6, [r4, #3236]
	.loc 4 183 0
	bl	FtlBbtMemInit
.LVL1039:
	.loc 4 184 0
	ldrh	r5, [r4, #74]
	subs	r5, r5, #1
	uxth	r5, r5
.LVL1040:
.L1261:
	.loc 4 184 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #74]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L1264
	.loc 4 185 0 is_stmt 1
	lsls	r3, r5, #10
	.loc 4 186 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 185 0
	str	r3, [r4, #3228]
	.loc 4 186 0
	bl	FlashReadPages
.LVL1041:
	.loc 4 187 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	bne	.L1262
	.loc 4 188 0
	ldr	r3, [r4, #3228]
	.loc 4 189 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 188 0
	adds	r3, r3, #1
	str	r3, [r4, #3228]
	.loc 4 189 0
	bl	FlashReadPages
.LVL1042:
.L1262:
	.loc 4 191 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	beq	.L1263
	.loc 4 196 0
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1263
	.loc 4 199 0
	ldr	r3, [r6, #4]
	.loc 4 198 0
	strh	r5, [r4, #128]	@ movhi
	.loc 4 199 0
	str	r3, [r4, #136]
	.loc 4 200 0
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #132]	@ movhi
.L1264:
	.loc 4 216 0
	ldrh	r3, [r4, #128]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1278
	.loc 4 222 0
	ldrh	r3, [r4, #132]
	cmp	r3, r2
	beq	.L1268
	.loc 4 224 0
	lsls	r3, r3, #10
	.loc 4 225 0
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1289+4
	.loc 4 224 0
	str	r3, [r4, #3228]
	.loc 4 225 0
	bl	FlashReadPages
.LVL1043:
	.loc 4 226 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	beq	.L1268
	.loc 4 229 0
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1268
	.loc 4 229 0 is_stmt 0 discriminator 1
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #136]
	cmp	r3, r2
	bls	.L1268
	.loc 4 231 0 is_stmt 1
	ldrh	r2, [r4, #132]
	.loc 4 232 0
	str	r3, [r4, #136]
	.loc 4 233 0
	ldrh	r3, [r6, #8]
	.loc 4 231 0
	strh	r2, [r4, #128]	@ movhi
	.loc 4 233 0
	strh	r3, [r4, #132]	@ movhi
.L1268:
	.loc 4 245 0
	ldr	r7, .L1289+4
	.loc 4 238 0
	movs	r1, #1
	ldrh	r0, [r4, #128]
	.loc 4 247 0
	movw	r8, #61649
	.loc 4 238 0
	bl	FtlGetLastWrittenPage
.LVL1044:
	sxth	r5, r0
.LVL1045:
	.loc 4 239 0
	adds	r0, r0, #1
	strh	r0, [r4, #130]	@ movhi
.L1270:
	.loc 4 241 0
	cmp	r5, #0
	bge	.L1273
	.loc 4 254 0
	movs	r2, #254
	ldr	r1, .L1289+8
	ldr	r0, .L1289+12
	bl	printk
.LVL1046:
.L1272:
	.loc 4 255 0
	ldrh	r3, [r6, #10]
	.loc 4 257 0
	ldrh	r0, [r6, #12]
	.loc 4 255 0
	strh	r3, [r4, #134]	@ movhi
	.loc 4 257 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1275
	.loc 4 259 0
	ldr	r2, [r4, #4]
	cmp	r0, r2
	beq	.L1275
	.loc 4 259 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1275
	.loc 4 261 0 is_stmt 1
	cmp	r0, r3
	bcs	.L1275
	.loc 4 263 0
	bl	FtlSysBlkNumInit
.LVL1047:
.L1275:
	ldr	r6, .L1289+16
	.loc 4 173 0 discriminator 1
	movs	r5, #0
.LVL1048:
.L1276:
	.loc 4 268 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	bcc	.L1277
	.loc 4 275 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1049:
.L1263:
	.loc 4 184 0 discriminator 2
	subs	r5, r5, #1
.LVL1050:
	uxth	r5, r5
.LVL1051:
	b	.L1261
.LVL1052:
.L1273:
	.loc 4 243 0
	ldrh	r3, [r4, #128]
	.loc 4 245 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 243 0
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3228]
	.loc 4 244 0
	ldr	r3, [r4, #2244]
	str	r3, [r4, #3232]
	.loc 4 245 0
	bl	FlashReadPages
.LVL1053:
	.loc 4 247 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	beq	.L1271
	.loc 4 247 0 is_stmt 0 discriminator 1
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L1272
.L1271:
	subs	r5, r5, #1
.LVL1054:
	sxth	r5, r5
.LVL1055:
	b	.L1270
.LVL1056:
.L1277:
	.loc 4 269 0 is_stmt 1 discriminator 3
	ldrh	r2, [r4, #3860]
	ldr	r1, [r4, #3232]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	.loc 4 268 0 discriminator 3
	adds	r5, r5, #1
	.loc 4 269 0 discriminator 3
	bl	ftl_memcpy
.LVL1057:
	b	.L1276
.LVL1058:
.L1278:
	.loc 4 218 0
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1059:
.L1290:
	.align	2
.L1289:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
	.word	.LANCHOR1+443
	.word	.LC1
	.word	.LANCHOR0+152
.LFE179:
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	1
	.global	FtlMakeBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
.LFB181:
	.loc 4 317 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI149:
	.loc 4 327 0
	mov	r8, #0
.LVL1060:
	ldr	r9, .L1310+4
	.loc 4 325 0
	bl	FtlBbtMemInit
.LVL1061:
	.loc 4 327 0
	sub	r5, r9, #156
	sub	r10, r9, #18
	mov	r4, r5
	.loc 4 326 0
	bl	FtlLoadFactoryBbt
.LVL1062:
.L1292:
	.loc 4 327 0 discriminator 1
	ldrh	r3, [r5, #30]
	cmp	r8, r3
	bcc	.L1298
	movs	r5, #0
.L1299:
	.loc 4 381 0 discriminator 1
	ldrh	r3, [r4, #90]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1300
	.loc 4 387 0
	ldrh	r5, [r4, #140]
	.loc 4 396 0
	movw	r6, #65535
	.loc 4 387 0
	subs	r5, r5, #1
	uxth	r5, r5
.L1301:
	.loc 4 387 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #140]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1305
	.loc 4 389 0 is_stmt 1
	mov	r0, r5
	bl	FtlBbmIsBadBlock
.LVL1063:
	cmp	r0, #1
	beq	.L1302
	.loc 4 391 0
	mov	r0, r5
	bl	FlashTestBlk
.LVL1064:
	cmp	r0, #0
	beq	.L1303
	.loc 4 393 0
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.LVL1065:
.L1302:
	.loc 4 387 0 discriminator 2
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1301
.L1298:
	.loc 4 331 0
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	.loc 4 328 0
	ldr	r0, [r5, #2244]
	.loc 4 329 0
	ldr	fp, [r5, #3244]
	.loc 4 331 0
	cmp	r3, r2
	.loc 4 328 0
	str	r0, [r5, #3232]
	.loc 4 329 0
	str	fp, [r5, #3236]
	.loc 4 331 0
	beq	.L1293
	.loc 4 333 0
	ldrh	r7, [r5, #74]
.LVL1066:
	.loc 4 335 0
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1310
	.loc 4 333 0
	mla	r7, r8, r7, r3
.LVL1067:
	.loc 4 334 0
	lsls	r3, r7, #10
	str	r3, [r5, #3228]
	.loc 4 335 0
	bl	FlashReadPages
.LVL1068:
	.loc 4 336 0
	ldrh	r2, [r5, #74]
	ldr	r1, [r5, #3232]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.LVL1069:
.L1294:
	.loc 4 376 0 discriminator 2
	uxth	r0, r7
	.loc 4 327 0 discriminator 2
	add	r8, r8, #1
	add	r9, r9, #4
	.loc 4 376 0 discriminator 2
	bl	FtlBbmMapBadBlock
.LVL1070:
	b	.L1292
.L1293:
	.loc 4 340 0
	mov	r1, r8
	bl	FlashGetBadBlockList
.LVL1071:
	.loc 4 341 0
	ldr	r1, [r9]
	ldr	r0, [r5, #3232]
	bl	FtlBbt2Bitmap
.LVL1072:
	.loc 4 343 0
	ldrh	r6, [r5, #74]
.L1296:
	.loc 4 347 0
	subs	r6, r6, #1
	uxth	r6, r6
.L1295:
	.loc 4 345 0
	ldrh	r0, [r4, #74]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
.LVL1073:
	cmp	r0, #1
	beq	.L1296
.LBB464:
.LBB465:
.LBB466:
	.loc 1 202 0
	movs	r1, #16
	ldr	r0, [r4, #3244]
.LBE466:
.LBE465:
.LBE464:
	.loc 4 349 0
	strh	r6, [r10]	@ movhi
.LBB469:
.LBB468:
.LBB467:
	.loc 1 202 0
	bl	__memzero
.LVL1074:
.LBE467:
.LBE468:
.LBE469:
.LBB470:
.LBB471:
.LBB472:
	mov	r1, #4096
	ldr	r0, [r4, #2244]
	bl	__memzero
.LVL1075:
.LBE472:
.LBE471:
.LBE470:
	.loc 4 352 0
	movw	r3, #61664
	strh	r3, [fp]	@ movhi
	.loc 4 353 0
	movs	r3, #0
	str	r3, [fp, #4]
	.loc 4 354 0
	ldrh	r3, [r10]
	.loc 4 355 0
	ldrh	r7, [r4, #74]
.LVL1076:
	.loc 4 357 0
	ldrh	r2, [r4, #3860]
	.loc 4 354 0
	strh	r3, [fp, #2]	@ movhi
	.loc 4 355 0
	ldrh	r3, [r10]
	.loc 4 357 0
	ldr	r1, [r9]
	lsls	r2, r2, #2
	ldr	r0, [r4, #3232]
	.loc 4 355 0
	mla	r7, r8, r7, r3
.LVL1077:
	.loc 4 356 0
	lsls	r3, r7, #10
	str	r3, [r4, #3228]
	.loc 4 357 0
	bl	ftl_memcpy
.LVL1078:
	.loc 4 358 0
	movs	r2, #1
	ldr	r0, .L1310
	mov	r1, r2
	bl	FlashEraseBlocks
.LVL1079:
	.loc 4 359 0
	movs	r3, #1
	ldr	r0, .L1310
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1080:
	.loc 4 360 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	bne	.L1294
	.loc 4 362 0
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
.LVL1081:
	.loc 4 363 0
	b	.L1295
.L1300:
	.loc 4 383 0 discriminator 3
	bl	FtlBbmMapBadBlock
.LVL1082:
	b	.L1299
.L1303:
	.loc 4 396 0
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	bne	.L1304
	.loc 4 398 0
	strh	r5, [r4, #128]	@ movhi
	b	.L1302
.L1304:
	.loc 4 402 0
	strh	r5, [r4, #132]	@ movhi
.L1305:
	.loc 4 414 0
	ldrh	r3, [r4, #128]
	.loc 4 411 0
	movs	r5, #0
	.loc 4 414 0
	ldr	r0, [r4, #3760]
	.loc 4 416 0
	movs	r1, #1
	.loc 4 413 0
	str	r5, [r4, #136]
	.loc 4 416 0
	movs	r2, #2
	.loc 4 411 0
	strh	r5, [r4, #130]	@ movhi
	.loc 4 414 0
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	.loc 4 415 0
	ldrh	r3, [r4, #132]
	lsls	r3, r3, #10
	str	r3, [r0, #24]
	.loc 4 416 0
	bl	FlashEraseBlocks
.LVL1083:
	.loc 4 418 0
	ldrh	r0, [r4, #128]
	bl	FtlBbmMapBadBlock
.LVL1084:
	.loc 4 419 0
	ldrh	r0, [r4, #132]
	bl	FtlBbmMapBadBlock
.LVL1085:
	.loc 4 421 0
	bl	FtlBbmTblFlush
.LVL1086:
	.loc 4 422 0
	ldr	r3, [r4, #136]
	.loc 4 425 0
	ldrh	r2, [r4, #132]
	.loc 4 426 0
	strh	r5, [r4, #130]	@ movhi
	.loc 4 422 0
	adds	r3, r3, #1
	str	r3, [r4, #136]
	.loc 4 424 0
	ldrh	r3, [r4, #128]
	.loc 4 425 0
	strh	r2, [r4, #128]	@ movhi
	.loc 4 427 0
	strh	r3, [r4, #132]	@ movhi
	.loc 4 429 0
	bl	FtlBbmTblFlush
.LVL1087:
	.loc 4 432 0
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1088:
.L1311:
	.align	2
.L1310:
	.word	.LANCHOR0+3224
	.word	.LANCHOR0+156
.LFE181:
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	1
	.global	FtlVendorPartWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
.LFB213:
	.loc 2 786 0
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1089:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI150:
	mov	r10, r2
	.loc 2 794 0
	ldr	r4, .L1324
	adds	r2, r0, r1
.LVL1090:
	.loc 2 786 0
	.pad #108
	sub	sp, sp, #108
.LCFI151:
	.loc 2 786 0
	mov	r8, r0
	mov	r6, r1
	.loc 2 794 0
	ldrh	r3, [r4, #72]
	cmp	r2, r3
	bhi	.L1320
	.loc 2 791 0
	ldrh	r7, [r4, #84]
	mov	r9, #0
	lsr	r7, r0, r7
.LVL1091:
	lsl	fp, r7, #2
.LVL1092:
.L1314:
	.loc 2 797 0
	cbnz	r6, .L1319
.L1312:
	.loc 2 820 0
	mov	r0, r9
	add	sp, sp, #108
.LCFI152:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1093:
.L1319:
.LCFI153:
	.loc 2 798 0
	ldr	r3, [r4, #3844]
	.loc 2 799 0
	mov	r0, r8
	.loc 2 798 0
	ldr	r2, [r3, fp]
.LVL1094:
	.loc 2 799 0
	ldrh	r3, [r4, #34]
	.loc 2 798 0
	str	r2, [sp, #12]
	.loc 2 799 0
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
.LVL1095:
	.loc 2 800 0
	ldr	r3, [sp, #8]
	.loc 2 801 0
	ldr	r2, [sp, #12]
.LVL1096:
	.loc 2 799 0
	str	r1, [sp, #4]
	.loc 2 800 0
	subs	r5, r3, r1
.LVL1097:
	uxth	r5, r5
.LVL1098:
	.loc 2 801 0
	cmp	r6, r5
	.loc 2 802 0
	it	cc
	uxthcc	r5, r6
	.loc 2 803 0
	cbz	r2, .L1316
	.loc 2 803 0 is_stmt 0 discriminator 1
	cmp	r5, r3
	beq	.L1316
	.loc 2 805 0 is_stmt 1
	ldr	r3, [r4, #3808]
	.loc 2 807 0
	add	r0, sp, #20
	.loc 2 804 0
	str	r2, [sp, #24]
	.loc 2 807 0
	movs	r2, #1
.LVL1099:
	mov	r1, r2
	.loc 2 805 0
	str	r3, [sp, #28]
	.loc 2 806 0
	add	r3, sp, #40
	str	r3, [sp, #32]
	.loc 2 807 0
	bl	FlashReadPages
.LVL1100:
.L1317:
	.loc 2 811 0
	lsls	r3, r5, #9
	ldr	r0, [r4, #3808]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	.loc 2 815 0
	subs	r6, r6, r5
	.loc 2 816 0
	add	r8, r8, r5
	add	fp, fp, #4
	.loc 2 811 0
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
.LVL1101:
	.loc 2 812 0
	mov	r1, r7
	ldr	r2, [r4, #3808]
	ldr	r0, .L1324+4
	.loc 2 814 0
	adds	r7, r7, #1
	.loc 2 812 0
	bl	FtlMapWritePage
.LVL1102:
	.loc 2 817 0
	ldr	r3, [sp, #8]
	.loc 2 813 0
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	.loc 2 817 0
	add	r10, r10, r3
	b	.L1314
.LVL1103:
.L1316:
	.loc 2 809 0
	ldrh	r2, [r4, #86]
.LVL1104:
	movs	r1, #0
	ldr	r0, [r4, #3808]
	bl	ftl_memset
.LVL1105:
	b	.L1317
.LVL1106:
.L1320:
	.loc 2 795 0
	mov	r9, #-1
	b	.L1312
.L1325:
	.align	2
.L1324:
	.word	.LANCHOR0
	.word	.LANCHOR0+3908
.LFE213:
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	1
	.global	Ftl_save_ext_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
.LFB236:
	.loc 2 2110 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 2 2111 0
	ldr	r2, .L1328
	ldr	r3, .L1328+4
	ldr	r1, [r2, #2616]
	cmp	r1, r3
	bne	.L1326
.LBB475:
.LBB476:
	.loc 2 2112 0
	ldr	r3, .L1328+8
	.loc 2 2125 0
	addw	r2, r2, #2616
	movs	r1, #1
	movs	r0, #0
	.loc 2 2112 0
	str	r3, [r2, #4]
	.loc 2 2113 0
	ldr	r3, [r2, #-152]
	str	r3, [r2, #88]
	.loc 2 2114 0
	ldr	r3, [r2, #-148]
	str	r3, [r2, #92]
	.loc 2 2115 0
	ldr	r3, [r2, #-156]
	str	r3, [r2, #8]
	.loc 2 2116 0
	ldr	r3, [r2, #-168]
	str	r3, [r2, #12]
	.loc 2 2117 0
	ldr	r3, [r2, #-176]
	str	r3, [r2, #16]
	.loc 2 2118 0
	ldr	r3, [r2, #-160]
	str	r3, [r2, #20]
	.loc 2 2119 0
	ldr	r3, [r2, #-132]
	str	r3, [r2, #28]
	.loc 2 2120 0
	ldr	r3, [r2, #-124]
	str	r3, [r2, #32]
	.loc 2 2121 0
	ldr	r3, [r2, #-172]
	str	r3, [r2, #36]
	.loc 2 2122 0
	ldr	r3, [r2, #-164]
	str	r3, [r2, #40]
	.loc 2 2123 0
	ldr	r3, [r2, #-120]
	str	r3, [r2, #44]
	.loc 2 2124 0
	ldr	r3, [r2, #-116]
	str	r3, [r2, #48]
	.loc 2 2125 0
	b	FtlVendorPartWrite
.LVL1107:
.L1326:
	bx	lr
.L1329:
	.align	2
.L1328:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177344
.LBE476:
.LBE475:
.LFE236:
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	1
	.global	FtlEctTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
.LFB215:
	.loc 2 861 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1108:
	.loc 2 864 0
	ldr	r2, .L1335
	.loc 2 861 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI154:
	.loc 2 864 0
	ldrh	r3, [r2, #-240]
	cmp	r3, #31
	.loc 2 865 0
	itett	ls
	addls	r3, r3, #1
	.loc 2 862 0
	movhi	r2, #32
.LVL1109:
	.loc 2 865 0
	strhls	r3, [r2, #-240]	@ movhi
.LVL1110:
	.loc 2 866 0
	movls	r2, #1
.LVL1111:
	ldr	r3, .L1335+4
	.loc 2 868 0
	cbnz	r0, .L1332
	.loc 2 868 0 is_stmt 0 discriminator 1
	ldr	r1, [r3, #3820]
	ldr	r0, [r1, #20]
.LVL1112:
	ldr	r1, [r1, #16]
	add	r2, r2, r0
.LVL1113:
	cmp	r1, r2
	bcc	.L1333
.LVL1114:
.L1332:
	.loc 2 870 0 is_stmt 1
	ldr	r2, [r3, #3820]
.LVL1115:
	.loc 2 875 0
	movs	r0, #64
.LVL1116:
	.loc 2 870 0
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	.loc 2 871 0
	ldr	r1, .L1335+8
	str	r1, [r2]
	.loc 2 872 0
	ldrh	r1, [r3, #3812]
	ldr	r2, [r3, #3820]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	.loc 2 873 0
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	.loc 2 874 0
	movs	r3, #0
	str	r3, [r2, #4]
	.loc 2 875 0
	bl	FtlVendorPartWrite
.LVL1117:
	.loc 2 876 0
	bl	Ftl_save_ext_data
.LVL1118:
.L1333:
	.loc 2 878 0
	movs	r0, #0
	pop	{r3, pc}
.L1336:
	.align	2
.L1335:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	1112818501
.LFE215:
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	1
	.global	FtlVendorPartRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
.LFB214:
	.loc 2 823 0
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1119:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI155:
	mov	r10, r2
	.loc 2 832 0
	ldr	r5, .L1346
	adds	r2, r0, r1
.LVL1120:
	.loc 2 823 0
	.pad #100
	sub	sp, sp, #100
.LCFI156:
	.loc 2 823 0
	mov	r8, r0
	mov	r7, r1
	.loc 2 832 0
	ldrh	r3, [r5, #72]
	cmp	r2, r3
	bhi	.L1345
	.loc 2 828 0
	ldrh	r6, [r5, #84]
	mov	r9, #0
	lsr	r6, r0, r6
.LVL1121:
	lsls	r6, r6, #2
.LVL1122:
.L1339:
	.loc 2 834 0
	cbnz	r7, .L1344
.L1337:
	.loc 2 857 0
	mov	r0, r9
	add	sp, sp, #100
.LCFI157:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1123:
.L1344:
.LCFI158:
	.loc 2 835 0
	ldr	r3, [r5, #3844]
	.loc 2 836 0
	mov	r0, r8
	ldrh	r4, [r5, #34]
.LVL1124:
	.loc 2 835 0
	ldr	r3, [r3, r6]
.LVL1125:
	.loc 2 836 0
	mov	r1, r4
	.loc 2 835 0
	str	r3, [sp, #4]
	.loc 2 836 0
	bl	__aeabi_uidivmod
.LVL1126:
	.loc 2 837 0
	subs	r4, r4, r1
	.loc 2 838 0
	ldr	r3, [sp, #4]
.LVL1127:
	.loc 2 837 0
	uxth	r4, r4
.LVL1128:
	.loc 2 836 0
	str	r1, [sp]
	.loc 2 838 0
	cmp	r7, r4
	.loc 2 839 0
	it	cc
	uxthcc	r4, r7
	lsl	fp, r4, #9
	.loc 2 840 0
	cbz	r3, .L1341
	.loc 2 841 0
	str	r3, [sp, #16]
	.loc 2 844 0
	movs	r2, #1
	.loc 2 842 0
	ldr	r3, [r5, #3808]
.LVL1129:
	.loc 2 844 0
	mov	r1, r2
	add	r0, sp, #12
	.loc 2 842 0
	str	r3, [sp, #20]
	.loc 2 843 0
	add	r3, sp, #32
	str	r3, [sp, #24]
	.loc 2 844 0
	bl	FlashReadPages
.LVL1130:
	.loc 2 845 0
	ldr	r3, [sp, #12]
	.loc 2 847 0
	mov	r2, fp
	ldr	r1, [r5, #3808]
	mov	r0, r10
	.loc 2 846 0
	adds	r3, r3, #1
	.loc 2 847 0
	ldr	r3, [sp]
	.loc 2 846 0
	it	eq
	moveq	r9, #-1
	.loc 2 847 0
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.LVL1131:
.L1343:
	.loc 2 852 0
	subs	r7, r7, r4
	.loc 2 853 0
	add	r8, r8, r4
	.loc 2 854 0
	add	r10, r10, fp
	adds	r6, r6, #4
	b	.L1339
.LVL1132:
.L1341:
	.loc 2 849 0
	mov	r2, fp
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
.LVL1133:
	b	.L1343
.LVL1134:
.L1345:
	.loc 2 833 0
	mov	r9, #-1
	b	.L1337
.L1347:
	.align	2
.L1346:
	.word	.LANCHOR0
.LFE214:
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	1
	.global	FtlLoadEctTbl
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
.LFB221:
	.loc 2 1334 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI159:
	.loc 2 1335 0
	movs	r0, #64
	ldr	r4, .L1350
	ldr	r2, [r4, #3820]
	ldrh	r1, [r4, #3812]
	bl	FtlVendorPartRead
.LVL1135:
	.loc 2 1336 0
	ldr	r3, [r4, #3820]
	ldr	r2, [r3]
	ldr	r3, .L1350+4
	cmp	r2, r3
	beq	.L1349
.LBB479:
.LBB480:
	.loc 2 1338 0
	ldr	r1, .L1350+8
	ldr	r0, .L1350+12
	bl	printk
.LVL1136:
	.loc 2 1339 0
	ldrh	r2, [r4, #3812]
	movs	r1, #0
	ldr	r0, [r4, #3820]
	lsls	r2, r2, #9
	bl	ftl_memset
.LVL1137:
.L1349:
.LBE480:
.LBE479:
	.loc 2 1344 0
	movs	r0, #0
	pop	{r4, pc}
.L1351:
	.align	2
.L1350:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC96
	.word	.LC71
.LFE221:
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	1
	.global	Ftl_load_ext_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
.LFB237:
	.loc 2 2130 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI160:
	.loc 2 2131 0
	movs	r1, #1
	ldr	r4, .L1355
	movs	r0, #0
	.loc 2 2132 0
	ldr	r5, .L1355+4
	.loc 2 2131 0
	addw	r2, r4, #2616
	bl	FtlVendorPartRead
.LVL1138:
	.loc 2 2132 0
	ldr	r3, [r4, #2616]
	cmp	r3, r5
	beq	.L1353
.LBB486:
.LBB487:
.LBB488:
	.loc 1 202 0
	mov	r1, #512
	addw	r0, r4, #2616
	bl	__memzero
.LVL1139:
.LBE488:
.LBE487:
.LBE486:
	.loc 2 2134 0
	str	r5, [r4, #2616]
.L1353:
	.loc 2 2137 0
	ldr	r3, [r4, #2616]
	cmp	r3, r5
	bne	.L1354
	.loc 2 2138 0
	ldr	r3, [r4, #2704]
	str	r3, [r4, #2464]
	.loc 2 2139 0
	ldr	r3, [r4, #2708]
	str	r3, [r4, #2468]
	.loc 2 2140 0
	ldr	r3, [r4, #2624]
	str	r3, [r4, #2460]
	.loc 2 2141 0
	ldr	r3, [r4, #2628]
	str	r3, [r4, #2448]
	.loc 2 2142 0
	ldr	r3, [r4, #2632]
	str	r3, [r4, #2440]
	.loc 2 2143 0
	ldr	r3, [r4, #2636]
	str	r3, [r4, #2456]
	.loc 2 2144 0
	ldr	r3, [r4, #2644]
	str	r3, [r4, #2484]
	.loc 2 2145 0
	ldr	r3, [r4, #2648]
	str	r3, [r4, #2492]
	.loc 2 2146 0
	ldr	r3, [r4, #2652]
	str	r3, [r4, #2444]
	.loc 2 2147 0
	ldr	r3, [r4, #2656]
	str	r3, [r4, #2452]
	.loc 2 2148 0
	ldr	r3, [r4, #2660]
	str	r3, [r4, #2496]
	.loc 2 2149 0
	ldr	r3, [r4, #2664]
	str	r3, [r4, #2500]
.L1354:
	.loc 2 2152 0
	ldrh	r2, [r4, #68]
	ldr	r3, [r4, #2484]
	ldr	r0, [r4, #2480]
	ldrh	r1, [r4, #16]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
.LVL1140:
	str	r0, [r4, #2488]
	pop	{r3, r4, r5, pc}
.L1356:
	.align	2
.L1355:
	.word	.LANCHOR0
	.word	1179929683
.LFE237:
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	1
	.global	FtlVpcTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
.LFB219:
	.loc 2 1004 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI161:
	.loc 2 1016 0
	movs	r5, #0
	.loc 2 1010 0
	ldr	r4, .L1369
	.loc 2 1047 0
	movs	r1, #255
	.loc 2 1005 0
	movw	r7, #65535
.LVL1141:
	.loc 2 1075 0
	ldr	r9, .L1369+16
	.loc 2 1010 0
	ldr	r3, [r4, #2244]
	.loc 2 1056 0
	addw	r8, r4, #3224
	.loc 2 1011 0
	ldr	r6, [r4, #3244]
	.loc 2 1075 0
	ldr	r10, .L1369+20
	.loc 2 1010 0
	str	r3, [r4, #3232]
	.loc 2 1013 0
	ldrh	r3, [r4, #2556]
	.loc 2 1011 0
	str	r6, [r4, #3236]
	.loc 2 1017 0
	str	r5, [r6, #12]
	.loc 2 1013 0
	strh	r3, [r6, #2]	@ movhi
	.loc 2 1014 0
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	.loc 2 1015 0
	ldr	r3, [r4, #2564]
	.loc 2 1016 0
	str	r5, [r6, #8]
	.loc 2 1025 0
	ldrh	r2, [r4, #2282]
	.loc 2 1015 0
	str	r3, [r6, #4]
	.loc 2 1019 0
	ldr	r3, .L1369+4
	str	r3, [r4, #2504]
	.loc 2 1020 0
	ldr	r3, .L1369+8
	str	r3, [r4, #2508]
	.loc 2 1021 0
	ldrh	r3, [r4, #2562]
	strh	r3, [r4, #2512]	@ movhi
	.loc 2 1022 0
	ldrh	r3, [r4, #30]
	strb	r3, [r4, #2514]
	.loc 2 1024 0
	ldrh	r3, [r4, #2280]
	strh	r3, [r4, #2518]	@ movhi
	.loc 2 1025 0
	ldrb	r3, [r4, #2286]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	.loc 2 1029 0
	ldrh	r2, [r4, #2330]
	.loc 2 1025 0
	strh	r3, [r4, #2520]	@ movhi
	.loc 2 1026 0
	ldrb	r3, [r4, #2288]	@ zero_extendqisi2
	strb	r3, [r4, #2515]
	.loc 2 1028 0
	ldrh	r3, [r4, #2328]
	strh	r3, [r4, #2522]	@ movhi
	.loc 2 1029 0
	ldrb	r3, [r4, #2334]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2524]	@ movhi
	.loc 2 1030 0
	ldrb	r3, [r4, #2336]	@ zero_extendqisi2
	strb	r3, [r4, #2516]
	.loc 2 1032 0
	ldrh	r3, [r4, #2376]
	.loc 2 1033 0
	ldrh	r2, [r4, #2378]
	.loc 2 1047 0
	ldr	r0, [r4, #3232]
	.loc 2 1032 0
	strh	r3, [r4, #2526]	@ movhi
	.loc 2 1033 0
	ldrb	r3, [r4, #2382]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	.loc 2 1047 0
	ldrh	r2, [r4, #86]
	.loc 2 1033 0
	strh	r3, [r4, #2528]	@ movhi
	.loc 2 1034 0
	ldrb	r3, [r4, #2384]	@ zero_extendqisi2
	strb	r3, [r4, #2517]
	.loc 2 1036 0
	ldr	r3, [r4, #2480]
	str	r3, [r4, #2536]
	.loc 2 1041 0
	ldr	r3, [r4, #2472]
	str	r3, [r4, #2544]
	.loc 2 1042 0
	ldr	r3, [r4, #2476]
	str	r3, [r4, #2540]
	.loc 2 1047 0
	bl	ftl_memset
.LVL1142:
	.loc 2 1048 0
	movs	r2, #48
	addw	r1, r4, #2504
	ldr	r0, [r4, #3232]
	bl	ftl_memcpy
.LVL1143:
	.loc 2 1049 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #3232]
	ldr	r1, [r4, #2260]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
.LVL1144:
	.loc 2 1050 0
	ldrh	r0, [r4, #16]
	ldr	r3, [r4, #3232]
	ldr	r1, [r4]
	lsrs	r2, r0, #3
	adds	r0, r0, #24
	lsls	r0, r0, #1
	adds	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r0, r3
	bl	ftl_memcpy
.LVL1145:
	.loc 2 1051 0
	mov	r0, r5
	bl	FtlUpdateVaildLpn
.LVL1146:
.L1358:
	.loc 2 1053 0
	ldr	r3, [r4, #2244]
	.loc 2 1056 0
	mov	r0, r8
	.loc 2 1055 0
	ldrh	r2, [r4, #2556]
	.loc 2 1053 0
	str	r3, [r4, #3232]
	.loc 2 1054 0
	ldr	r3, [r4, #3244]
	str	r3, [r4, #3236]
	.loc 2 1055 0
	ldrh	r3, [r4, #2558]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3228]
	.loc 2 1056 0
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1147:
	.loc 2 1057 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r4, #2558]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1359
	.loc 2 1059 0
	ldrh	r3, [r4, #2556]
	.loc 2 1058 0
	ldrh	r7, [r4, #2560]
	.loc 2 1059 0
	strh	r3, [r4, #2560]	@ movhi
	.loc 2 1060 0
	movs	r3, #0
	strh	r3, [r4, #2558]	@ movhi
	.loc 2 1061 0
	bl	FtlFreeSysBlkQueueOut
.LVL1148:
	.loc 2 1066 0
	ldr	r3, [r4, #2472]
	.loc 2 1061 0
	strh	r0, [r4, #2556]	@ movhi
	.loc 2 1066 0
	adds	r2, r3, #1
	str	r3, [r4, #2564]
	str	r2, [r4, #2472]
	.loc 2 1067 0
	lsls	r2, r0, #10
	str	r2, [r4, #3228]
	.loc 2 1068 0
	str	r3, [r6, #4]
	.loc 2 1070 0
	movs	r3, #1
	.loc 2 1069 0
	strh	r0, [r6, #2]	@ movhi
	.loc 2 1070 0
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
.LVL1149:
.L1359:
	.loc 2 1072 0
	ldrh	r3, [r4, #2558]
	.loc 2 1073 0
	ldr	r2, [r4, #3224]
	.loc 2 1072 0
	adds	r3, r3, #1
	uxth	r3, r3
	.loc 2 1073 0
	adds	r2, r2, #1
	.loc 2 1072 0
	strh	r3, [r4, #2558]	@ movhi
	.loc 2 1073 0
	bne	.L1360
	.loc 2 1075 0
	cmp	r3, #1
	bne	.L1361
	.loc 2 1075 0 is_stmt 0 discriminator 1
	movw	r2, #1075
	mov	r1, r9
	mov	r0, r10
	bl	printk
.LVL1150:
.L1361:
	.loc 2 1076 0 is_stmt 1
	ldrh	r3, [r4, #2558]
	.loc 2 1078 0
	adds	r5, r5, #1
.LVL1151:
	uxth	r5, r5
.LVL1152:
	.loc 2 1076 0
	cmp	r3, #1
	.loc 2 1077 0
	ittt	eq
	ldrheq	r3, [r4, #80]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2558]	@ movhi
	.loc 2 1079 0
	cmp	r5, #3
	bls	.L1358
	.loc 2 1080 0
	mov	r2, r5
	ldr	r1, [r4, #3228]
	ldr	r0, .L1369+12
	bl	printk
.LVL1153:
.L1364:
	b	.L1364
.L1360:
	.loc 2 1086 0
	cmp	r3, #1
	beq	.L1358
	.loc 2 1089 0
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1365
	.loc 2 1090 0
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.LVL1154:
.L1365:
	.loc 2 1092 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL1155:
.L1370:
	.align	2
.L1369:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177344
	.word	.LC97
	.word	.LANCHOR1+454
	.word	.LC1
.LFE219:
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	1
	.global	FtlVpcCheckAndModify
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
.LFB239:
	.loc 2 2192 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
.LCFI162:
	.loc 2 2199 0
	movs	r5, #0
.LVL1156:
	.loc 2 2198 0
	ldr	r4, .L1383
	.loc 2 2197 0
	ldr	r1, .L1383+4
	ldr	r0, .L1383+8
	bl	printk
.LVL1157:
	.loc 2 2198 0
	ldrh	r2, [r4, #18]
	movs	r1, #0
	ldr	r0, [r4, #3824]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL1158:
.L1372:
	.loc 2 2199 0 discriminator 1
	ldr	r3, [r4, #2436]
	cmp	r5, r3
	bcc	.L1374
	.loc 2 2209 0 discriminator 1
	ldr	r9, .L1383+12
	movs	r7, #0
	.loc 2 2207 0 discriminator 1
	movw	r8, #65535
.LVL1159:
.L1375:
	.loc 2 2206 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r5, r7
.LVL1160:
	cmp	r3, r5
	bhi	.L1379
	.loc 2 2218 0
	add	sp, sp, #12
.LCFI163:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.LVL1161:
.L1374:
.LCFI164:
	.loc 2 2200 0
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
.LVL1162:
	.loc 2 2201 0
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1373
	.loc 2 2202 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1163:
	.loc 2 2203 0
	ldr	r2, [r4, #3824]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1373:
	.loc 2 2199 0 discriminator 2
	adds	r5, r5, #1
	b	.L1372
.LVL1164:
.L1379:
	.loc 2 2207 0
	ldr	r3, [r4, #2260]
	uxth	r6, r7
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #3824]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r2, r3
	beq	.L1377
	.loc 2 2207 0 is_stmt 0 discriminator 1
	cmp	r2, r8
	beq	.L1377
	.loc 2 2209 0 is_stmt 1
	mov	r1, r6
	mov	r0, r9
	bl	printk
.LVL1165:
	.loc 2 2210 0
	ldrh	r3, [r4, #2280]
	cmp	r3, r5
	beq	.L1377
	.loc 2 2210 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2376]
	cmp	r3, r5
	beq	.L1377
	.loc 2 2210 0 discriminator 2
	ldrh	r3, [r4, #2328]
	cmp	r3, r5
	beq	.L1377
	.loc 2 2211 0 is_stmt 1
	ldr	r3, [r4, #3824]
	.loc 2 2212 0
	mov	r0, r5
	.loc 2 2211 0
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #2260]
	strh	r2, [r3, r6, lsl #1]	@ movhi
	.loc 2 2212 0
	bl	update_vpc_list
.LVL1166:
	.loc 2 2213 0
	bl	l2p_flush
.LVL1167:
	.loc 2 2214 0
	bl	FtlVpcTblFlush
.LVL1168:
.L1377:
	adds	r7, r7, #1
	b	.L1375
.L1384:
	.align	2
.L1383:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC87
	.word	.LC98
.LFE239:
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	1
	.global	allocate_new_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
.LFB249:
	.loc 2 2631 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1169:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI165:
	.loc 2 2631 0
	mov	r6, r0
	.loc 2 2633 0
	ldr	r4, .L1395
	.loc 2 2632 0
	ldrh	r5, [r0]
.LVL1170:
	.loc 2 2633 0
	ldrh	r3, [r4, #16]
	cmp	r3, r5
	bcs	.L1386
	.loc 2 2633 0 is_stmt 0 discriminator 1
	movw	r2, #2633
	ldr	r1, .L1395+4
	ldr	r0, .L1395+8
	bl	printk
.LVL1171:
.L1386:
	.loc 2 2635 0 is_stmt 1
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1387
	.loc 2 2636 0
	ldr	r3, [r4, #2260]
	.loc 2 2637 0
	mov	r0, r5
	.loc 2 2636 0
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1388
	.loc 2 2637 0
	bl	INSERT_DATA_LIST
.LVL1172:
.L1387:
	.loc 2 2642 0
	ldrh	r0, [r4, #3952]
	.loc 2 2641 0
	movs	r3, #1
	strb	r3, [r6, #8]
	.loc 2 2642 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1389
	.loc 2 2643 0
	cmp	r5, r0
	bne	.L1390
	.loc 2 2643 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1391
.L1390:
	.loc 2 2644 0 is_stmt 1
	bl	update_vpc_list
.LVL1173:
.L1391:
	.loc 2 2645 0
	movw	r3, #65535
	strh	r3, [r4, #3952]	@ movhi
.L1389:
	.loc 2 2647 0
	mov	r0, r6
	bl	allocate_data_superblock
.LVL1174:
	.loc 2 2648 0
	bl	l2p_flush
.LVL1175:
	.loc 2 2649 0
	movs	r0, #0
	bl	FtlEctTblFlush
.LVL1176:
	.loc 2 2650 0
	bl	FtlVpcTblFlush
.LVL1177:
	.loc 2 2652 0
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.LVL1178:
.L1388:
	.loc 2 2639 0
	bl	INSERT_FREE_LIST
.LVL1179:
	b	.L1387
.L1396:
	.align	2
.L1395:
	.word	.LANCHOR0
	.word	.LANCHOR1+490
	.word	.LC1
.LFE249:
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	1
	.global	FtlProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
.LFB165:
	.loc 3 902 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1180:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI166:
	.loc 3 911 0
	movs	r7, #0
.LVL1181:
	ldr	r6, .L1411
	.loc 3 902 0
	.pad #20
	sub	sp, sp, #20
.LCFI167:
	.loc 3 902 0
	mov	r4, r3
	.loc 3 909 0
	movs	r2, #0
.LVL1182:
	.loc 3 931 0
	ldr	r10, .L1411+8
	.loc 3 902 0
	mov	r5, r0
	.loc 3 931 0
	ldr	fp, .L1411+12
	.loc 3 902 0
	mov	r9, r1
	.loc 3 909 0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
.LVL1183:
	bl	FlashProgPages
.LVL1184:
.L1398:
	.loc 3 911 0 discriminator 1
	cmp	r7, r9
	beq	.L1405
	.loc 3 917 0
	mov	r8, #0
	b	.L1406
.LVL1185:
.L1401:
	.loc 3 913 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1186:
	.loc 3 914 0
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1399
	.loc 3 915 0
	ldr	r1, [r6, #2260]
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	.loc 3 916 0
	ldrh	r3, [r6, #78]
	.loc 3 917 0
	strb	r8, [r4, #6]
	.loc 3 918 0
	strh	r8, [r4, #4]	@ movhi
	.loc 3 916 0
	strh	r3, [r4, #2]	@ movhi
.L1399:
	.loc 3 920 0
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1400
	.loc 3 921 0
	mov	r0, r4
	bl	allocate_new_data_superblock
.LVL1187:
.L1400:
	.loc 3 924 0
	ldr	r3, [r6, #2712]
	adds	r3, r3, #1
	str	r3, [r6, #2712]
	.loc 3 925 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
.LVL1188:
	.loc 3 926 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1189:
	.loc 3 928 0
	movs	r2, #0
	.loc 3 927 0
	str	r0, [r5, #4]
	.loc 3 926 0
	str	r0, [sp, #12]
	.loc 3 928 0
	movs	r1, #1
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r0, r5
	bl	FlashProgPages
.LVL1190:
.L1406:
	.loc 3 912 0
	ldr	r3, [r5]
.LVL1191:
	adds	r3, r3, #1
	beq	.L1401
	.loc 3 931 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
	cmp	r2, r3
	bcc	.L1402
	.loc 3 931 0 is_stmt 0 discriminator 1
	movw	r2, #931
	mov	r1, r10
	mov	r0, fp
	bl	printk
.LVL1192:
.L1402:
	.loc 3 932 0 is_stmt 1
	ldr	r3, [r5, #4]
	add	r1, sp, #16
	.loc 3 933 0
	movs	r2, #1
	ldr	r0, [r5, #16]
	.loc 3 932 0
	str	r3, [r1, #-4]!
.LVL1193:
	.loc 3 933 0
	bl	log2phys
.LVL1194:
	.loc 3 935 0
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #12]
.LVL1195:
	.loc 3 937 0
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #4]
	bl	P2V_block_in_plane
.LVL1196:
	.loc 3 938 0
	ldr	r3, [sp, #4]
.LVL1197:
	.loc 3 937 0
	mov	r8, r0
	.loc 3 938 0
	adds	r3, r3, #1
	beq	.L1403
	.loc 3 940 0
	ldr	r3, [r6, #2260]
.LVL1198:
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1404
	.loc 3 941 0
	mov	r1, r0
	ldr	r0, .L1411+4
	bl	printk
.LVL1199:
.L1404:
	.loc 3 942 0
	mov	r0, r8
	bl	decrement_vpc_count
.LVL1200:
.L1403:
	.loc 3 911 0 discriminator 2
	adds	r7, r7, #1
	adds	r5, r5, #20
	b	.L1398
.LVL1201:
.L1405:
	.loc 3 946 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
.LVL1202:
	cmp	r2, r3
	bcc	.L1397
	.loc 3 946 0 is_stmt 0 discriminator 1
	movw	r2, #946
	ldr	r1, .L1411+8
	ldr	r0, .L1411+12
	bl	printk
.LVL1203:
.L1397:
	.loc 3 947 0 is_stmt 1
	add	sp, sp, #20
.LCFI168:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1204:
.L1412:
	.align	2
.L1411:
	.word	.LANCHOR0
	.word	.LC99
	.word	.LANCHOR1+519
	.word	.LC1
.LFE165:
	.fnend
	.size	FtlProgPages, .-FtlProgPages
	.align	1
	.global	FtlCacheWriteBack
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
.LFB166:
	.loc 3 950 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI169:
	.loc 3 951 0
	ldr	r4, .L1418
	ldr	r0, [r4, #3948]
	cbz	r0, .L1414
	.loc 3 952 0
	ldr	r3, .L1418+4
	movs	r2, #0
	movs	r1, #1
	ldr	r3, [r3, #-236]
	bl	FtlProgPages
.LVL1205:
	.loc 3 953 0
	movs	r3, #0
	str	r3, [r4, #3948]
.L1414:
	.loc 3 956 0
	movs	r0, #0
	pop	{r4, pc}
.L1419:
	.align	2
.L1418:
	.word	.LANCHOR0
	.word	.LANCHOR3
.LFE166:
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	1
	.global	FtlSysFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
.LFB162:
	.loc 3 797 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI170:
	.loc 3 798 0
	bl	FtlCacheWriteBack
.LVL1206:
	.loc 3 799 0
	bl	l2p_flush
.LVL1207:
	.loc 3 800 0
	movs	r0, #1
	bl	FtlEctTblFlush
.LVL1208:
	.loc 3 801 0
	bl	FtlVpcTblFlush
.LVL1209:
	.loc 3 803 0
	movs	r0, #0
	pop	{r3, pc}
.LFE162:
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	1
	.global	sftl_deinit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
	.fnstart
.LFB163:
	.loc 3 806 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI171:
	.loc 3 807 0
	ldr	r3, .L1423
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1422
	.loc 3 808 0
	bl	FtlSysFlush
.LVL1210:
.L1422:
	.loc 3 810 0
	movs	r0, #0
	pop	{r3, pc}
.L1424:
	.align	2
.L1423:
	.word	.LANCHOR2
.LFE163:
	.fnend
	.size	sftl_deinit, .-sftl_deinit
	.align	1
	.global	FtlDiscard
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
.LFB168:
	.loc 3 1154 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1211:
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
.LCFI172:
.LVL1212:
	.loc 3 1161 0
	adds	r2, r0, r1
	ldr	r6, .L1440
	.loc 3 1154 0
	mov	r7, r0
	mov	r5, r1
	.loc 3 1161 0
	ldr	r3, [r6, #116]
	cmp	r2, r3
	bhi	.L1433
	.loc 3 1164 0
	cmp	r1, #31
	bhi	.L1427
.LVL1213:
.L1432:
	.loc 3 1165 0
	movs	r0, #0
.L1425:
	.loc 3 1198 0
	add	sp, sp, #8
.LCFI173:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1214:
.L1427:
.LCFI174:
	.loc 3 1167 0
	bl	FtlCacheWriteBack
.LVL1215:
	.loc 3 1169 0
	ldrh	r4, [r6, #34]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidiv
.LVL1216:
	.loc 3 1170 0
	smulbb	r3, r0, r4
	.loc 3 1169 0
	mov	r8, r0
.LVL1217:
	.loc 3 1170 0
	subs	r7, r7, r3
.LVL1218:
	uxth	r7, r7
.LVL1219:
	.loc 3 1171 0
	cbz	r7, .L1428
	.loc 3 1172 0
	subs	r4, r4, r7
	.loc 3 1173 0
	add	r8, r0, #1
	.loc 3 1172 0
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	.loc 3 1174 0
	subs	r5, r5, r4
.L1428:
	.loc 3 1181 0
	ldr	r4, .L1440+4
	.loc 3 1177 0
	mov	r3, #-1
	str	r3, [sp, #4]
.LVL1220:
.L1429:
	.loc 3 1178 0
	ldrh	r3, [r6, #34]
	cmp	r5, r3
	bcs	.L1431
	.loc 3 1191 0
	ldr	r3, .L1440+4
	ldr	r2, [r3, #-232]
	cmp	r2, #32
	bls	.L1432
	.loc 3 1193 0
	movs	r4, #0
	str	r4, [r3, #-232]
	.loc 3 1194 0
	bl	l2p_flush
.LVL1221:
	.loc 3 1195 0
	bl	FtlVpcTblFlush
.LVL1222:
	b	.L1432
.L1431:
	.loc 3 1179 0
	movs	r2, #0
	mov	r1, sp
	mov	r0, r8
	bl	log2phys
.LVL1223:
	.loc 3 1180 0
	ldr	r3, [sp]
	adds	r3, r3, #1
.LVL1224:
	beq	.L1430
	.loc 3 1181 0
	ldr	r3, [r4, #-232]
.LVL1225:
	.loc 3 1183 0
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r8
	.loc 3 1181 0
	adds	r3, r3, #1
	str	r3, [r4, #-232]
	.loc 3 1182 0
	ldr	r3, [r6, #2444]
	adds	r3, r3, #1
	str	r3, [r6, #2444]
	.loc 3 1183 0
	bl	log2phys
.LVL1226:
	.loc 3 1184 0
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1227:
	.loc 3 1185 0
	bl	decrement_vpc_count
.LVL1228:
.L1430:
	.loc 3 1188 0
	ldrh	r3, [r6, #34]
.LVL1229:
	.loc 3 1187 0
	add	r8, r8, #1
	.loc 3 1188 0
	subs	r5, r5, r3
	b	.L1429
.LVL1230:
.L1433:
	.loc 3 1162 0
	mov	r0, #-1
	b	.L1425
.L1441:
	.align	2
.L1440:
	.word	.LANCHOR0
	.word	.LANCHOR3
.LFE168:
	.fnend
	.size	FtlDiscard, .-FtlDiscard
	.align	1
	.global	FtlGcFreeTempBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
.LFB258:
	.loc 5 112 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1231:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI175:
.LVL1232:
	.loc 5 119 0
	movw	r2, #65535
	.loc 5 115 0
	ldr	r4, .L1468
	.loc 5 119 0
	ldrh	r5, [r4, #2376]
	.loc 5 115 0
	ldrh	r1, [r4, #78]
.LVL1233:
	.loc 5 119 0
	cmp	r5, r2
	bne	.L1443
.LVL1234:
.L1453:
	.loc 5 153 0
	ldrh	r2, [r4, #2376]
	.loc 5 152 0
	movs	r3, #0
	str	r3, [r4, #3960]
	.loc 5 153 0
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1466
.L1444:
	.loc 5 154 0
	movs	r0, #0
.L1442:
	.loc 5 199 0
	add	sp, sp, #12
.LCFI176:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1235:
.L1443:
.LCFI177:
	.loc 5 121 0
	cbz	r0, .L1446
	.loc 5 123 0
	ldr	r3, .L1468+4
	ldrh	r0, [r3, #4]
.LVL1236:
	cmp	r0, r2
	beq	.L1447
.L1448:
	.loc 5 122 0
	movs	r1, #2
.LVL1237:
.L1446:
	.loc 5 131 0
	ldr	r0, .L1468+8
.LVL1238:
	bl	FtlGcScanTempBlk
.LVL1239:
	str	r0, [sp, #4]
.LVL1240:
	.loc 5 132 0
	adds	r0, r0, #1
	beq	.L1449
	.loc 5 135 0
	ldr	r2, [r4, #2248]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L1450
	.loc 5 136 0
	subs	r3, r3, #5
	.loc 5 137 0
	movs	r0, #1
	.loc 5 136 0
	strh	r3, [r2, r5, lsl #1]	@ movhi
	.loc 5 137 0
	bl	FtlEctTblFlush
.LVL1241:
.L1450:
	.loc 5 139 0
	ldr	r3, [r4, #3960]
	cbnz	r3, .L1451
	.loc 5 140 0
	ldr	r3, [r4, #2712]
	.loc 5 141 0
	ldr	r0, [sp, #4]
	.loc 5 140 0
	adds	r3, r3, #1
	.loc 5 141 0
	ubfx	r0, r0, #10, #16
	.loc 5 140 0
	str	r3, [r4, #2712]
	.loc 5 141 0
	bl	FtlBbmMapBadBlock
.LVL1242:
	.loc 5 142 0
	bl	FtlBbmTblFlush
.LVL1243:
.L1451:
	.loc 5 144 0
	movs	r3, #0
	str	r3, [r4, #3960]
.L1462:
	.loc 5 149 0
	movs	r0, #1
	b	.L1442
.LVL1244:
.L1447:
	.loc 5 124 0
	movs	r2, #0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 125 0
	ldrh	r3, [r4, #2276]
	cmp	r3, #17
	bhi	.L1448
	b	.L1446
.LVL1245:
.L1449:
	.loc 5 148 0
	ldr	r3, .L1468+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1462
	b	.L1453
.LVL1246:
.L1466:
	.loc 5 160 0
	bl	FtlCacheWriteBack
.LVL1247:
	.loc 5 162 0
	ldrb	r1, [r4, #2383]	@ zero_extendqisi2
	ldrh	r3, [r4, #78]
	ldrh	r2, [r4, #3180]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1454
	.loc 5 162 0 is_stmt 0 discriminator 1
	movs	r2, #162
	ldr	r1, .L1468+12
	ldr	r0, .L1468+16
	bl	printk
.LVL1248:
.L1454:
	.loc 5 163 0 is_stmt 1
	ldrb	r3, [r4, #2383]	@ zero_extendqisi2
	.loc 5 165 0
	movs	r6, #0
	.loc 5 163 0
	ldrh	r0, [r4, #78]
	.loc 5 166 0
	mov	r9, #12
	.loc 5 163 0
	ldr	r2, [r4, #2260]
	ldrh	r1, [r4, #2376]
	.loc 5 168 0
	ldr	r10, .L1468+12
	.loc 5 163 0
	smulbb	r3, r3, r0
	.loc 5 168 0
	ldr	fp, .L1468+16
	.loc 5 163 0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 5 164 0
	ldr	r2, [r4, #2460]
	ldrh	r3, [r4, #3180]
	add	r3, r3, r2
	str	r3, [r4, #2460]
.LVL1249:
.L1455:
	.loc 5 165 0 discriminator 1
	ldrh	r2, [r4, #3180]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1459
	.loc 5 181 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1250:
	.loc 5 182 0
	ldrh	r0, [r4, #2376]
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1460
	.loc 5 183 0
	bl	INSERT_DATA_LIST
.LVL1251:
.L1461:
	.loc 5 187 0
	movs	r5, #0
.LVL1252:
	.loc 5 186 0
	movw	r6, #65535
	strh	r6, [r4, #2376]	@ movhi
	.loc 5 187 0
	strh	r5, [r4, #3180]	@ movhi
	.loc 5 188 0
	strh	r5, [r4, #3172]	@ movhi
	.loc 5 189 0
	bl	l2p_flush
.LVL1253:
	.loc 5 190 0
	bl	FtlVpcTblFlush
.LVL1254:
	.loc 5 191 0
	ldrh	r3, [r4, #2552]
	ldrh	r2, [r4, #2276]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L1444
	.loc 5 193 0
	movs	r3, #20
	.loc 5 192 0
	strh	r6, [r4, #2568]	@ movhi
	.loc 5 193 0
	strh	r3, [r4, #3128]	@ movhi
	b	.L1444
.LVL1255:
.L1459:
	.loc 5 166 0
	uxth	r7, r6
	ldr	r8, [r4, #3176]
	.loc 5 168 0
	ldr	r3, [r4, #2436]
	.loc 5 166 0
	mul	r7, r9, r7
	add	r5, r8, r7
	.loc 5 168 0
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1456
	.loc 5 168 0 is_stmt 0 discriminator 1
	movs	r2, #168
	mov	r1, r10
	mov	r0, fp
	bl	printk
.LVL1256:
.L1456:
	.loc 5 169 0 is_stmt 1
	movs	r2, #0
	add	r1, sp, #4
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1257:
	.loc 5 170 0
	ldr	r3, [sp, #4]
	ldr	r0, [r8, r7]
	cmp	r0, r3
	bne	.L1457
	.loc 5 171 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1258:
	.loc 5 172 0
	movs	r2, #1
	.loc 5 171 0
	mov	r7, r0
	.loc 5 172 0
	adds	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1259:
	.loc 5 173 0
	mov	r0, r7
.L1467:
	.loc 5 177 0
	bl	decrement_vpc_count
.LVL1260:
.L1458:
	adds	r6, r6, #1
	b	.L1455
.L1457:
	.loc 5 175 0
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1458
	.loc 5 177 0
	ldrh	r0, [r4, #2376]
	b	.L1467
.L1460:
	.loc 5 185 0
	bl	INSERT_FREE_LIST
.LVL1261:
	b	.L1461
.L1469:
	.align	2
.L1468:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2376
	.word	.LANCHOR1+532
	.word	.LC1
.LFE258:
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	1
	.global	FtlGcPageRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
.LFB261:
	.loc 5 289 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI178:
	.loc 5 290 0
	ldr	r4, .L1472
	ldrh	r1, [r4, #78]
	addw	r0, r4, #2376
	bl	FtlGcScanTempBlk
.LVL1262:
	.loc 5 291 0
	ldrh	r2, [r4, #2378]
	ldrh	r3, [r4, #78]
	cmp	r2, r3
	bcc	.L1470
.LBB491:
.LBB492:
	.loc 5 293 0
	addw	r0, r4, #3864
	bl	FtlMapBlkWriteDump_data
.LVL1263:
	.loc 5 294 0
	movs	r0, #0
	bl	FtlGcFreeTempBlock
.LVL1264:
	.loc 5 295 0
	movs	r3, #0
	str	r3, [r4, #3960]
.L1470:
	pop	{r4, pc}
.L1473:
	.align	2
.L1472:
	.word	.LANCHOR0
.LBE492:
.LBE491:
.LFE261:
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	1
	.global	FtlPowerLostRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
.LFB233:
	.loc 2 2032 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI179:
	.loc 2 2033 0
	movs	r5, #0
	ldr	r4, .L1475
	.loc 2 2035 0
	addw	r6, r4, #2280
	.loc 2 2033 0
	str	r5, [r4, #3972]
	.loc 2 2035 0
	mov	r0, r6
	.loc 2 2037 0
	addw	r4, r4, #2328
	.loc 2 2035 0
	bl	FtlRecoverySuperblock
.LVL1265:
	.loc 2 2036 0
	mov	r0, r6
	bl	FtlSlcSuperblockCheck
.LVL1266:
	.loc 2 2037 0
	mov	r0, r4
	bl	FtlRecoverySuperblock
.LVL1267:
	.loc 2 2038 0
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
.LVL1268:
	.loc 2 2039 0
	bl	FtlGcPageRecovery
.LVL1269:
	.loc 2 2040 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1270:
	.loc 2 2042 0
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1476:
	.align	2
.L1475:
	.word	.LANCHOR0
.LFE233:
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	1
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
.LFB263:
	.loc 5 316 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI180:
	.loc 5 320 0
	movs	r3, #0
	ldr	r4, .L1484
	.loc 5 321 0
	movs	r5, #0
	.loc 5 322 0
	movs	r6, #20
	.loc 5 320 0
	mov	r2, r3
	ldr	r1, [r4, #3144]
	ldr	r0, [r4, #3796]
	bl	FlashProgPages
.LVL1271:
.L1478:
	.loc 5 321 0 discriminator 1
	ldr	r1, [r4, #3144]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1481
	.loc 5 335 0
	ldr	r0, [r4, #3796]
	bl	FtlGcBufFree
.LVL1272:
	.loc 5 337 0
	ldrh	r3, [r4, #2380]
	.loc 5 336 0
	movs	r0, #0
	str	r0, [r4, #3144]
	.loc 5 337 0
	cbnz	r3, .L1477
	.loc 5 338 0
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1273:
	b	.L1483
.L1481:
	.loc 5 322 0
	muls	r3, r6, r3
	ldr	r2, [r4, #3796]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	adds	r0, r2, #1
	bne	.L1479
	.loc 5 323 0
	ldrh	r0, [r4, #2376]
	movs	r5, #0
	ldr	r1, [r4, #2260]
	strh	r5, [r1, r0, lsl #1]	@ movhi
	.loc 5 324 0
	strh	r2, [r4, #2376]	@ movhi
	.loc 5 325 0
	ldr	r2, [r4, #2712]
	adds	r2, r2, #1
	str	r2, [r4, #2712]
	.loc 5 326 0
	ldr	r2, [r4, #3796]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL1274:
	.loc 5 327 0
	bl	FtlBbmTblFlush
.LVL1275:
	.loc 5 328 0
	bl	FtlGcPageVarInit
.LVL1276:
.L1483:
	.loc 5 339 0
	movs	r0, #1
.L1477:
	.loc 5 342 0
	pop	{r4, r5, r6, pc}
.L1479:
	.loc 5 331 0 discriminator 2
	ldr	r3, [r1, #12]
.LVL1277:
	.loc 5 332 0 discriminator 2
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL1278:
	b	.L1478
.L1485:
	.align	2
.L1484:
	.word	.LANCHOR0
.LFE263:
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	1
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
.LFB262:
	.loc 5 300 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI181:
	.loc 5 301 0
	movw	r3, #65535
	ldr	r4, .L1489
	ldrh	r2, [r4, #2376]
	cmp	r2, r3
	beq	.L1487
	.loc 5 301 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2380]
	cbnz	r3, .L1488
.L1487:
	.loc 5 302 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1279:
	.loc 5 303 0
	movs	r0, #0
	.loc 5 304 0
	movs	r5, #0
	.loc 5 303 0
	bl	FtlGcFreeTempBlock
.LVL1280:
	.loc 5 305 0
	ldr	r0, .L1489+4
	.loc 5 304 0
	strb	r5, [r4, #2384]
	.loc 5 305 0
	bl	allocate_data_superblock
.LVL1281:
	.loc 5 306 0
	strh	r5, [r4, #3172]	@ movhi
	.loc 5 307 0
	strh	r5, [r4, #3180]	@ movhi
	.loc 5 308 0
	bl	l2p_flush
.LVL1282:
	.loc 5 309 0
	mov	r0, r5
	bl	FtlEctTblFlush
.LVL1283:
	.loc 5 310 0
	bl	FtlVpcTblFlush
.LVL1284:
.L1488:
	.loc 5 312 0
	ldr	r0, .L1489+4
	.loc 5 313 0
	pop	{r3, r4, r5, lr}
.LCFI182:
	.loc 5 312 0
	b	get_new_active_ppa
.LVL1285:
.L1490:
	.align	2
.L1489:
	.word	.LANCHOR0
	.word	.LANCHOR0+2376
.LFE262:
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	1
	.global	rk_ftl_garbage_collect
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
.LFB269:
	.loc 5 470 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1286:
	.loc 5 478 0
	ldr	r3, .L1610
	.loc 5 470 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI183:
	mov	r7, r0
	.pad #36
	sub	sp, sp, #36
.LCFI184:
	mov	r4, r3
	.loc 5 478 0
	ldr	r0, [r3, #3780]
.LVL1287:
	cmp	r0, #0
	bne	.L1566
	.loc 5 481 0
	ldrh	r2, [r3, #2268]
	cmp	r2, #47
	bls	.L1491
	.loc 5 485 0
	ldr	r2, .L1610+4
	ldrh	r1, [r2, #4]
.LVL1288:
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1493
.L1496:
.LBB496:
.LBB497:
	.loc 5 489 0
	ldrh	r2, [r4, #3184]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1494
.L1495:
	.loc 5 496 0
	ldr	r3, [r4, #3132]
	.loc 5 497 0
	movw	r5, #65535
	ldrh	r1, [r4, #2568]
	.loc 5 496 0
	adds	r3, r3, #1
	add	r3, r3, r7, lsl #7
	.loc 5 497 0
	cmp	r1, r5
	.loc 5 496 0
	str	r3, [r4, #3132]
	.loc 5 497 0
	bne	.L1497
	ldrh	r2, [r4, #2376]
	cmp	r2, r1
	bne	.L1498
	ldrh	r8, [r4, #3182]
	cmp	r8, r2
	bne	.L1499
	.loc 5 498 0
	ldrh	r2, [r4, #2276]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1499
.LBB498:
	.loc 5 502 0
	ldr	r3, .L1610+8
	.loc 5 501 0
	movs	r6, #0
	str	r6, [r4, #3132]
	.loc 5 502 0
	strh	r6, [r3, #-228]	@ movhi
	.loc 5 503 0
	bl	GetSwlReplaceBlock
.LVL1289:
	.loc 5 504 0
	cmp	r0, r8
	.loc 5 503 0
	mov	r5, r0
	.loc 5 504 0
	bne	.L1570
	.loc 5 505 0
	ldrh	r2, [r4, #2276]
	ldrh	r3, [r4, #3130]
	cmp	r2, r3
	bcs	.L1502
	.loc 5 506 0
	movs	r0, #64
	bl	List_get_gc_head_node
.LVL1290:
	.loc 5 507 0
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1504
	.loc 5 508 0
	mov	r0, r3
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1505
	.loc 5 509 0
	mov	r0, r6
	bl	List_get_gc_head_node
.LVL1291:
	uxth	r6, r0
	.loc 5 510 0
	movs	r3, #128
	strh	r3, [r4, #3130]	@ movhi
	.loc 5 520 0
	cmp	r6, r5
	beq	.L1504
.L1501:
	.loc 5 521 0
	ldr	r3, [r4, #2260]
	mov	r5, r6
	ldrh	r1, [r4, #3128]
	ldrh	r2, [r4, #2276]
	ldrh	r3, [r3, r6, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2248]
	ldr	r0, .L1610+12
	ldrh	r1, [r1, r6, lsl #1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.LVL1292:
	b	.L1504
.L1493:
.LBE498:
.LBE497:
.LBE496:
	.loc 5 485 0 discriminator 1
	ldrh	r3, [r3, #2376]
	cmp	r3, r2
	beq	.L1496
	.loc 5 486 0
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1293:
	cmp	r0, #0
	beq	.L1496
	.loc 5 487 0
	movs	r0, #1
	b	.L1491
.L1494:
.LBB504:
.LBB501:
	.loc 5 489 0
	ldrh	r3, [r4, #3182]
	cmp	r3, r1
	.loc 5 490 0
	itt	eq
	strheq	r2, [r4, #3182]	@ movhi
	.loc 5 491 0
	strheq	r3, [r4, #3184]	@ movhi
	b	.L1495
.L1505:
.LBB499:
	.loc 5 512 0
	movs	r3, #64
.L1603:
	.loc 5 516 0
	strh	r3, [r4, #3130]	@ movhi
.L1504:
	.loc 5 523 0
	bl	FtlGcReFreshBadBlk
.LVL1294:
.LBE499:
	.loc 5 527 0
	cmp	r7, #0
	bne	.L1507
	movw	r3, #65535
	cmp	r5, r3
	bne	.L1507
.L1564:
	.loc 5 528 0
	ldrh	r3, [r4, #2276]
	cmp	r3, #24
	bhi	.L1571
	.loc 5 530 0
	cmp	r3, #16
	ldrh	r6, [r4, #78]
	bls	.L1509
	.loc 5 531 0
	lsrs	r6, r6, #5
.LVL1295:
.L1508:
	.loc 5 539 0
	ldrh	r2, [r4, #3128]
	cmp	r2, r3
	bcs	.L1512
	.loc 5 540 0
	ldrh	r3, [r4, #2376]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1513
	ldrh	r2, [r4, #3182]
	cmp	r2, r3
	bne	.L1513
	.loc 5 542 0
	ldr	r3, .L1610+8
	ldrh	r0, [r3, #-228]
	cbnz	r0, .L1514
	ldr	r3, [r4, #2436]
	ldr	r2, [r4, #2432]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1515
.L1514:
	.loc 5 543 0
	ldrh	r3, [r4, #2552]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1604:
	.loc 5 545 0
	strh	r3, [r4, #3128]	@ movhi
	.loc 5 546 0
	movs	r3, #0
	str	r3, [r4, #3140]
.LVL1296:
.L1491:
.LBE501:
.LBE504:
	.loc 5 758 0
	add	sp, sp, #36
.LCFI185:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1297:
.L1502:
.LCFI186:
.LBB505:
.LBB502:
.LBB500:
	.loc 5 516 0
	movs	r3, #80
	b	.L1603
.L1570:
	.loc 5 504 0
	mov	r6, r0
	b	.L1501
.L1509:
.LBE500:
	.loc 5 532 0
	cmp	r3, #12
	bls	.L1510
	.loc 5 533 0
	lsrs	r6, r6, #4
.LVL1298:
	b	.L1508
.LVL1299:
.L1510:
	.loc 5 534 0
	cmp	r3, #8
	bls	.L1508
	.loc 5 535 0
	lsrs	r6, r6, #2
.LVL1300:
	b	.L1508
.LVL1301:
.L1571:
	.loc 5 529 0
	movs	r6, #1
.LVL1302:
	b	.L1508
.L1515:
	.loc 5 545 0
	movs	r3, #18
	b	.L1604
.L1513:
	.loc 5 549 0
	ldrh	r3, [r4, #2552]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3128]	@ movhi
.L1512:
	.loc 5 551 0
	ldrh	r3, [r4, #3186]
	cbz	r3, .L1572
	.loc 5 552 0
	adds	r6, r6, #32
.LVL1303:
	uxth	r6, r6
.LVL1304:
.L1572:
	.loc 5 551 0
	movw	r5, #65535
.L1518:
	.loc 5 597 0
	ldrh	r3, [r4, #2568]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1527
	.loc 5 598 0
	cmp	r5, r3
	beq	.L1528
	.loc 5 599 0
	strh	r5, [r4, #2568]	@ movhi
.L1529:
	.loc 5 611 0
	ldrh	r0, [r4, #2568]
	movw	r7, #65535
.LVL1305:
	.loc 5 610 0
	movs	r3, #0
	strb	r3, [r4, #2576]
	.loc 5 611 0
	cmp	r0, r7
	beq	.L1527
	.loc 5 612 0
	bl	IsBlkInGcList
.LVL1306:
	cbz	r0, .L1532
	.loc 5 613 0
	strh	r7, [r4, #2568]	@ movhi
.L1532:
	.loc 5 615 0
	ldrh	r2, [r4, #2568]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1527
	.loc 5 616 0
	ldr	r0, .L1610+16
	bl	make_superblock
.LVL1307:
	.loc 5 619 0
	ldr	r2, .L1610+8
	.loc 5 617 0
	movs	r3, #0
	strh	r3, [r4, #2570]	@ movhi
	.loc 5 618 0
	strb	r3, [r4, #2574]
	.loc 5 619 0
	strh	r3, [r2, #-226]	@ movhi
	.loc 5 620 0
	ldrh	r1, [r4, #2568]
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-224]	@ movhi
.LVL1308:
.L1527:
	.loc 5 625 0
	ldrh	r3, [r4, #2568]
	ldrh	r2, [r4, #2280]
	cmp	r2, r3
	beq	.L1533
	ldrh	r2, [r4, #2328]
	cmp	r2, r3
	beq	.L1533
	.loc 5 666 0
	ldr	r7, .L1610+8
.LVL1309:
.L1534:
	.loc 5 631 0
	ldrh	r9, [r4, #2568]
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1535
	.loc 5 632 0
	movs	r3, #0
	.loc 5 647 0
	mov	r10, r3
.LVL1310:
	.loc 5 632 0
	str	r3, [r4, #3140]
.L1536:
	.loc 5 635 0
	ldrh	r8, [r4, #3136]
.LVL1311:
	mov	r0, r8
	bl	List_get_gc_head_node
.LVL1312:
	uxth	fp, r0
	.loc 5 636 0
	cmp	fp, r9
	.loc 5 635 0
	strh	fp, [r4, #2568]	@ movhi
	.loc 5 636 0
	bne	.L1537
	.loc 5 637 0
	movs	r3, #0
	.loc 5 638 0
	movs	r0, #8
	.loc 5 637 0
	strh	r3, [r4, #3136]	@ movhi
	b	.L1491
.LVL1313:
.L1519:
	.loc 5 558 0
	movs	r3, #0
	.loc 5 560 0
	ldrh	r7, [r4, #2276]
.LVL1314:
	.loc 5 558 0
	str	r3, [r4, #3140]
	.loc 5 560 0
	ldrh	r3, [r4, #3128]
	ldr	r5, .L1610+8
	cmp	r3, r7
	bcs	.L1520
	.loc 5 561 0
	ldrh	r3, [r5, #-228]
	cbnz	r3, .L1521
	ldr	r3, [r4, #2436]
	ldr	r2, [r4, #2432]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1522
.L1521:
	.loc 5 562 0
	ldrh	r3, [r4, #2552]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1605:
	.loc 5 564 0
	strh	r3, [r4, #3128]	@ movhi
.L1607:
	.loc 5 578 0
	bl	FtlReadRefresh
.LVL1315:
	.loc 5 579 0
	ldrh	r0, [r5, #-228]
	b	.L1491
.L1522:
	.loc 5 564 0
	movs	r3, #18
	b	.L1605
.L1520:
	.loc 5 572 0
	ldrh	r0, [r5, #-228]
	cmp	r0, #0
	bne	.L1524
	.loc 5 573 0
	ldrh	r6, [r4, #2552]
	add	r3, r6, r6, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3128]	@ movhi
	.loc 5 574 0
	bl	List_get_gc_head_node
.LVL1316:
	.loc 5 575 0
	ldr	r3, [r4, #2260]
	uxth	r0, r0
	ldrh	r1, [r4, #80]
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r4, #8]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1525
	.loc 5 576 0
	subs	r6, r6, #1
	.loc 5 575 0
	cmp	r7, r6
	bge	.L1607
.L1525:
	.loc 5 584 0
	cmp	r2, #0
	bne	.L1524
	.loc 5 585 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1317:
	.loc 5 586 0
	ldrh	r0, [r4, #2276]
	adds	r0, r0, #1
	b	.L1491
.LVL1318:
.L1528:
	.loc 5 600 0
	ldrh	r3, [r4, #3182]
	cmp	r3, r5
	beq	.L1529
	.loc 5 604 0
	ldr	r2, [r4, #2260]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1530
	.loc 5 605 0
	strh	r5, [r4, #3182]	@ movhi
.L1530:
	.loc 5 606 0
	ldrh	r3, [r4, #3182]
	strh	r3, [r4, #2568]	@ movhi
	.loc 5 607 0
	movw	r3, #65535
	strh	r3, [r4, #3182]	@ movhi
	b	.L1529
.L1611:
	.align	2
.L1610:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC100
	.word	.LANCHOR0+2568
.L1533:
	.loc 5 626 0
	movw	r3, #65535
	strh	r3, [r4, #2568]	@ movhi
.LVL1319:
.L1608:
	.loc 5 652 0
	ldr	r3, .L1612
	ldrh	r0, [r3, #-228]
	b	.L1491
.LVL1320:
.L1537:
	str	r0, [sp, #8]
	.loc 5 640 0
	mov	r0, fp
	bl	IsBlkInGcList
.LVL1321:
	add	r8, r8, #1
	ldr	r3, [sp, #8]
	cbz	r0, .L1538
	.loc 5 641 0
	strh	r8, [r4, #3136]	@ movhi
	b	.L1536
.L1538:
	.loc 5 645 0
	uxth	r0, r3
	ldrh	ip, [r4, #8]
	ldrh	r3, [r4, #78]
	.loc 5 644 0
	uxth	r8, r8
	.loc 5 645 0
	ldr	r2, [r4, #2260]
	.loc 5 644 0
	strh	r8, [r4, #3136]	@ movhi
	.loc 5 645 0
	mul	r3, ip, r3
	ldrh	r1, [r2, r0, lsl #1]
	add	ip, r3, r3, lsr #31
	cmp	r1, ip, asr #1
	bgt	.L1540
	.loc 5 646 0
	cmp	r8, #48
	bls	.L1541
	cmp	r1, #8
	bls	.L1541
	ldrh	r1, [r4, #3172]
	cmp	r1, #35
	bhi	.L1541
.L1540:
	.loc 5 647 0
	strh	r10, [r4, #3136]	@ movhi
.L1541:
	.loc 5 649 0
	ldrh	r2, [r2, r0, lsl #1]
	cmp	r3, r2
	bgt	.L1542
	cmp	r5, r9
	bne	.L1542
	.loc 5 651 0
	movs	r3, #0
	.loc 5 650 0
	strh	r5, [r4, #2568]	@ movhi
	.loc 5 651 0
	strh	r3, [r4, #3136]	@ movhi
	b	.L1608
.L1542:
	.loc 5 655 0
	cbnz	r2, .L1543
	.loc 5 656 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1322:
	.loc 5 657 0
	ldrh	r3, [r4, #3136]
	adds	r3, r3, #1
	strh	r3, [r4, #3136]	@ movhi
	b	.L1536
.L1543:
	.loc 5 660 0
	movs	r3, #0
	strb	r3, [r4, #2576]
	.loc 5 662 0
	ldrh	r3, [r4, #2280]
	cmp	r3, fp
	bne	.L1544
	movw	r2, #662
	ldr	r1, .L1612+4
	ldr	r0, .L1612+8
	bl	printk
.LVL1323:
.L1544:
	.loc 5 663 0
	ldrh	r2, [r4, #2568]
	ldrh	r3, [r4, #2328]
	cmp	r2, r3
	bne	.L1545
	movw	r2, #663
	ldr	r1, .L1612+4
	ldr	r0, .L1612+8
	bl	printk
.LVL1324:
.L1545:
	.loc 5 664 0
	ldrh	r2, [r4, #2568]
	ldrh	r3, [r4, #2376]
	cmp	r2, r3
	bne	.L1546
	mov	r2, #664
	ldr	r1, .L1612+4
	ldr	r0, .L1612+8
	bl	printk
.LVL1325:
.L1546:
	.loc 5 665 0
	ldr	r0, .L1612+12
	bl	make_superblock
.LVL1326:
	.loc 5 667 0
	ldrh	r1, [r4, #2568]
	.loc 5 666 0
	movs	r3, #0
	.loc 5 667 0
	ldr	r2, [r4, #2260]
	.loc 5 666 0
	strh	r3, [r7, #-226]	@ movhi
	.loc 5 667 0
	ldrh	r2, [r2, r1, lsl #1]
	.loc 5 668 0
	strh	r3, [r4, #2570]	@ movhi
	.loc 5 669 0
	strb	r3, [r4, #2574]
	.loc 5 667 0
	strh	r2, [r7, #-224]	@ movhi
.LVL1327:
.L1535:
	.loc 5 675 0
	bl	FtlReadRefresh
.LVL1328:
	.loc 5 678 0
	movs	r3, #1
	str	r3, [r4, #3780]
	.loc 5 679 0
	ldrh	r3, [r4, #78]
.LVL1329:
	str	r3, [sp, #12]
	.loc 5 680 0
	ldrh	r3, [r4, #2570]
.LVL1330:
	ldr	r1, [sp, #12]
.LVL1331:
	adds	r2, r3, r6
	cmp	r2, r1
	.loc 5 681 0
	itt	gt
	movgt	r2, r1
	subgt	r6, r2, r3
.LVL1332:
	.loc 5 693 0
	mov	r3, #0
	.loc 5 681 0
	it	gt
	uxthgt	r6, r6
.LVL1333:
.L1606:
	str	r3, [sp, #8]
	.loc 5 683 0
	ldrh	r3, [sp, #8]
	cmp	r6, r3
	bls	.L1556
	.loc 5 688 0
	ldr	r3, [sp, #8]
	.loc 5 687 0
	movw	r10, #65535
.LVL1334:
	.loc 5 688 0
	ldrh	r1, [r4, #2570]
	mov	fp, #20
	.loc 5 685 0
	ldrh	lr, [r4, #8]
	.loc 5 688 0
	ldr	r0, [r4, #3160]
	add	r1, r1, r3
	movs	r3, #0
	ldr	ip, .L1612+16
	mov	r8, r3
	b	.L1557
.LVL1335:
.L1550:
	.loc 5 686 0
	ldrh	r2, [ip, #2]!
.LVL1336:
	.loc 5 687 0
	cmp	r2, r10
	beq	.L1549
	.loc 5 688 0
	mla	r9, fp, r8, r0
	.loc 5 689 0
	add	r8, r8, #1
.LVL1337:
	.loc 5 688 0
	orr	r2, r1, r2, lsl #10
.LVL1338:
	.loc 5 689 0
	uxth	r8, r8
.LVL1339:
	.loc 5 688 0
	str	r2, [r9, #4]
.LVL1340:
.L1549:
	adds	r3, r3, #1
.LVL1341:
.L1557:
	.loc 5 685 0
	uxth	r2, r3
.LVL1342:
	cmp	lr, r2
	bhi	.L1550
	.loc 5 693 0
	mov	fp, #0
	ldrb	r2, [r4, #2576]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.LVL1343:
.L1551:
	.loc 5 695 0
	uxth	r3, fp
	cmp	r8, r3
	bhi	.L1555
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L1606
.L1555:
	mov	r9, #20
	.loc 5 696 0
	ldr	r3, [r4, #3160]
	mul	r9, r9, fp
	add	r2, r3, r9
	.loc 5 697 0
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	beq	.L1552
	.loc 5 696 0
	ldr	r10, [r2, #12]
	.loc 5 697 0
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1552
	.loc 5 699 0
	ldr	r3, [r10, #8]
.LVL1344:
	.loc 5 700 0
	adds	r2, r3, #1
	bne	.L1553
	mov	r2, #700
	ldr	r1, .L1612+4
	ldr	r0, .L1612+8
	str	r3, [sp, #16]
	bl	printk
.LVL1345:
	ldr	r3, [sp, #16]
.LVL1346:
.L1553:
	.loc 5 701 0
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #28
	bl	log2phys
.LVL1347:
	.loc 5 702 0
	ldr	r0, [r4, #3160]
	ldr	r3, [sp, #28]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1552
	.loc 5 704 0
	ldrh	r3, [r7, #-226]
	.loc 5 705 0
	movs	r2, #20
	ldr	r1, [r4, #3144]
	str	r2, [sp, #20]
	.loc 5 704 0
	adds	r3, r3, #1
	strh	r3, [r7, #-226]	@ movhi
	.loc 5 705 0
	ldr	r3, [r4, #3796]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
	str	r1, [r3, #16]
	str	r3, [sp, #16]
	.loc 5 706 0
	bl	Ftl_get_new_temp_ppa
.LVL1348:
	ldr	r3, [sp, #16]
	.loc 5 707 0
	ldr	r1, [r4, #3796]
	ldr	r2, [sp, #20]
	.loc 5 706 0
	str	r0, [r3, #4]
	.loc 5 707 0
	ldr	r3, [r4, #3144]
	mla	r3, r2, r3, r1
	ldr	r2, [r4, #3160]
	add	r2, r2, r9
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	.loc 5 716 0
	movs	r1, #1
	.loc 5 708 0
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	.loc 5 709 0
	ldr	r3, [sp, #28]
.LVL1349:
	str	r3, [r10, #12]
	.loc 5 710 0
	ldrh	r3, [r4, #2376]
.LVL1350:
	strh	r3, [r10, #2]	@ movhi
	.loc 5 713 0
	ldr	r3, [r4, #2476]
	.loc 5 716 0
	ldr	r0, [r4, #3160]
	.loc 5 713 0
	str	r3, [r10, #4]
	.loc 5 715 0
	ldr	r3, [r4, #3144]
	.loc 5 716 0
	add	r0, r0, r9
	.loc 5 715 0
	adds	r3, r3, #1
	str	r3, [r4, #3144]
	.loc 5 716 0
	bl	FtlGcBufAlloc
.LVL1351:
	.loc 5 717 0
	ldrb	r2, [r4, #2383]	@ zero_extendqisi2
	ldr	r3, [r4, #3144]
	cmp	r2, r3
	beq	.L1554
	ldrh	r3, [r4, #2380]
	cbnz	r3, .L1552
.L1554:
	.loc 5 718 0
	bl	Ftl_gc_temp_data_write_back
.LVL1352:
	cbz	r0, .L1552
.L1609:
	.loc 5 733 0
	movs	r3, #0
	.loc 5 734 0
	ldrh	r0, [r7, #-228]
	.loc 5 733 0
	str	r3, [r4, #3780]
	b	.L1491
.L1552:
	add	fp, fp, #1
	b	.L1551
.LVL1353:
.L1556:
	.loc 5 727 0
	ldrh	r3, [r4, #2570]
	add	r6, r6, r3
.LVL1354:
	.loc 5 729 0
	ldr	r3, [sp, #12]
.LVL1355:
	.loc 5 727 0
	uxth	r6, r6
	.loc 5 729 0
	cmp	r3, r6
	.loc 5 727 0
	strh	r6, [r4, #2570]	@ movhi
	.loc 5 729 0
	bls	.L1558
	ldrh	r2, [r7, #-226]
	ldrh	r3, [r7, #-224]
.LVL1356:
	cmp	r2, r3
	bne	.L1559
.LVL1357:
.L1558:
	.loc 5 731 0
	ldr	r3, [r4, #3144]
.LVL1358:
	cbz	r3, .L1560
	.loc 5 732 0
	bl	Ftl_gc_temp_data_write_back
.LVL1359:
	cmp	r0, #0
	bne	.L1609
.L1560:
	.loc 5 737 0
	ldrh	r1, [r7, #-226]
	cbnz	r1, .L1561
	ldrh	r2, [r4, #2568]
	ldr	r3, [r4, #2260]
	ldrh	r0, [r3, r2, lsl #1]
	cbz	r0, .L1561
	.loc 5 739 0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	.loc 5 740 0
	ldrh	r0, [r4, #2568]
	bl	update_vpc_list
.LVL1360:
	.loc 5 741 0
	bl	FtlCacheWriteBack
.LVL1361:
	.loc 5 742 0
	bl	l2p_flush
.LVL1362:
	.loc 5 743 0
	bl	FtlVpcTblFlush
.LVL1363:
.L1561:
	.loc 5 745 0
	movw	r3, #65535
	strh	r3, [r4, #2568]	@ movhi
.L1559:
	.loc 5 747 0
	ldrh	r3, [r4, #2276]
	cmp	r3, #2
	bhi	.L1562
	.loc 5 749 0
	ldrh	r6, [r4, #78]
.LVL1364:
	b	.L1534
.LVL1365:
.L1562:
	.loc 5 754 0
	ldrh	r0, [r7, #-228]
	.loc 5 752 0
	movs	r2, #0
	str	r2, [r4, #3780]
	.loc 5 754 0
	cmp	r0, #0
	bne	.L1491
	.loc 5 757 0
	adds	r0, r3, #1
	b	.L1491
.LVL1366:
.L1566:
.LBE502:
.LBE505:
	.loc 5 479 0
	movs	r0, #0
	b	.L1491
.L1613:
	.align	2
.L1612:
	.word	.LANCHOR3
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2568
	.word	.LANCHOR0+2582
.LVL1367:
.L1497:
.LBB506:
.LBB503:
	.loc 5 527 0
	cmp	r7, #0
	beq	.L1564
.L1507:
	.loc 5 557 0
	ldrh	r2, [r4, #2376]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1575
.L1565:
	ldrh	r3, [r4, #3182]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1575
	cmp	r5, r3
	bne	.L1575
	ldrh	r3, [r4, #2568]
	cmp	r3, r5
	beq	.L1519
.L1524:
	.loc 5 556 0
	movw	r5, #65535
.L1575:
	movs	r6, #1
.LVL1368:
	b	.L1518
.LVL1369:
.L1499:
	.loc 5 527 0
	cmp	r7, #0
	beq	.L1564
	movw	r5, #65535
	b	.L1565
.L1498:
	cmp	r7, #0
	bne	.L1524
	b	.L1564
.LBE503:
.LBE506:
.LFE269:
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	1
	.global	sftl_gc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
	.fnstart
.LFB270:
	.loc 5 761 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 5 762 0
	movs	r1, #1
	mov	r0, r1
	b	rk_ftl_garbage_collect
.LVL1370:
.LFE270:
	.fnend
	.size	sftl_gc, .-sftl_gc
	.align	1
	.global	FtlRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	.fnstart
.LFB164:
	.loc 3 813 0
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1371:
	.loc 3 823 0
	cmp	r0, #16
	.loc 3 813 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI187:
	.pad #60
	sub	sp, sp, #60
.LCFI188:
	.loc 3 813 0
	mov	r7, r1
	mov	r10, r3
	str	r2, [sp, #28]
	.loc 3 823 0
	bne	.L1616
	.loc 3 824 0
	mov	r2, r3
.LVL1372:
	ldr	r1, [sp, #28]
	add	r0, r7, #256
.LVL1373:
	bl	FtlVendorPartRead
.LVL1374:
	mov	r8, r0
.LVL1375:
.L1615:
	.loc 3 900 0
	mov	r0, r8
.LVL1376:
	add	sp, sp, #60
.LCFI189:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1377:
.L1616:
.LCFI190:
	.loc 3 829 0
	ldr	r3, [sp, #28]
.LVL1378:
	ldr	r4, .L1647
	adds	r3, r1, r3
.LVL1379:
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #116]
	cmp	r2, r3
	bhi	.L1636
	.loc 3 829 0 is_stmt 0 discriminator 1
	ldr	r3, .L1647+4
	ldr	r3, [r3]
	adds	r5, r3, #1
	beq	.L1637
	.loc 3 832 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1380:
	.loc 3 834 0
	ldrh	r5, [r4, #34]
	mov	r0, r7
	.loc 3 827 0
	mov	r9, #0
.LVL1381:
	.loc 3 839 0
	mov	r6, r9
.LVL1382:
	.loc 3 814 0
	mov	r8, r9
	.loc 3 834 0
	mov	r1, r5
	bl	__aeabi_uidiv
.LVL1383:
	.loc 3 835 0
	ldr	r3, [sp, #8]
	mov	r1, r5
	.loc 3 834 0
	str	r0, [sp, #12]
	.loc 3 835 0
	subs	r0, r3, #1
	bl	__aeabi_uidiv
.LVL1384:
	.loc 3 836 0
	ldr	r3, [sp, #12]
.LVL1385:
	.loc 3 834 0
	ldr	r5, [sp, #12]
.LVL1386:
	.loc 3 835 0
	str	r0, [sp, #16]
	.loc 3 836 0
	rsb	r3, r3, #1
.LVL1387:
	.loc 3 827 0
	str	r9, [sp, #24]
.LVL1388:
	.loc 3 836 0
	add	r3, r3, r0
.LVL1389:
	.loc 3 819 0
	str	r9, [sp, #20]
	.loc 3 836 0
	str	r3, [sp, #4]
	.loc 3 840 0
	ldr	r2, [sp, #4]
.LVL1390:
	ldr	r3, [r4, #2440]
	add	r3, r3, r2
	str	r3, [r4, #2440]
.LVL1391:
.L1618:
	.loc 3 841 0
	ldr	r3, [sp, #4]
.LVL1392:
	cbnz	r3, .L1634
	.loc 3 897 0
	ldr	r3, .L1647
.LVL1393:
	ldrh	r2, [r3, #3186]
	cbnz	r2, .L1635
	.loc 3 897 0 is_stmt 0 discriminator 1
	ldrh	r3, [r3, #2276]
	cmp	r3, #31
	bhi	.L1615
.L1635:
	.loc 3 898 0 is_stmt 1
	movs	r1, #1
	movs	r0, #0
.LVL1394:
	bl	rk_ftl_garbage_collect
.LVL1395:
	b	.L1615
.LVL1396:
.L1634:
	.loc 3 842 0
	movs	r2, #0
	add	r1, sp, #52
	mov	r0, r5
.LVL1397:
	bl	log2phys
.LVL1398:
	.loc 3 843 0
	ldr	r2, [sp, #52]
	adds	r0, r2, #1
	bne	.L1619
	mov	fp, #0
.LVL1399:
.L1620:
	.loc 3 844 0 discriminator 1
	ldrh	r0, [r4, #34]
.LVL1400:
	cmp	fp, r0
	bcc	.L1622
.L1623:
	.loc 3 873 0
	ldr	r3, [sp, #4]
.LVL1401:
	.loc 3 871 0
	adds	r5, r5, #1
	.loc 3 873 0
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1627
	.loc 3 873 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #8]
.LVL1402:
	cmp	r6, r3, lsl #2
	bne	.L1618
.LVL1403:
.L1627:
	.loc 3 874 0 is_stmt 1
	cmp	r6, #0
	beq	.L1618
	.loc 3 875 0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3792]
	.loc 3 876 0
	mov	fp, #0
.LVL1404:
	.loc 3 875 0
	bl	FlashReadPages
.LVL1405:
	.loc 3 882 0
	lsl	r3, r9, #9
	str	r3, [sp, #40]
	.loc 3 879 0
	ldr	r3, [sp, #20]
.LVL1406:
	lsls	r3, r3, #9
.LVL1407:
	str	r3, [sp, #32]
	ldr	r3, [sp, #24]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
.L1633:
	movs	r3, #20
	.loc 3 877 0
	ldr	r2, [r4, #3792]
	mul	r3, r3, fp
	ldr	r0, [sp, #12]
.LVL1408:
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1629
	.loc 3 878 0
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3776]
	cmp	r1, r2
	bne	.L1630
	str	r3, [sp, #44]
	.loc 3 879 0
	mov	r0, r10
.LVL1409:
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.LVL1410:
.L1646:
	.loc 3 882 0
	bl	ftl_memcpy
.LVL1411:
	ldr	r3, [sp, #44]
.LVL1412:
.L1630:
	.loc 3 884 0
	ldr	r2, [r4, #3792]
	ldr	r0, [r2, r3]
.LVL1413:
	add	ip, r2, r3
	adds	r1, r0, #1
	.loc 3 885 0
	itttt	eq
	ldreq	r1, [r4, #2688]
	.loc 3 886 0
	moveq	r8, r0
	.loc 3 885 0
	addeq	r1, r1, #1
	streq	r1, [r4, #2688]
	.loc 3 888 0
	ldr	r3, [r2, r3]
	cmp	r3, #256
	bne	.L1632
.LBB513:
	.loc 3 889 0
	ldr	r0, [ip, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1414:
	.loc 3 890 0
	bl	FtlGcRefreshBlock
.LVL1415:
.L1632:
.LBE513:
	.loc 3 876 0 discriminator 2
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1633
	.loc 3 893 0
	movs	r6, #0
	b	.L1618
.LVL1416:
.L1622:
	.loc 3 845 0
	mla	r0, r0, r5, fp
.LVL1417:
	.loc 3 846 0
	cmp	r7, r0
	bhi	.L1621
	.loc 3 846 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1621
	.loc 3 847 0 is_stmt 1
	subs	r0, r0, r7
.LVL1418:
.LBB514:
.LBB515:
.LBB516:
	.loc 1 202 0
	mov	r1, #512
	add	r0, r10, r0, lsl #9
	bl	__memzero
.LVL1419:
.L1621:
.LBE516:
.LBE515:
.LBE514:
	.loc 3 844 0 discriminator 2
	add	fp, fp, #1
	b	.L1620
.LVL1420:
.L1619:
	.loc 3 850 0
	ldr	r3, [r4, #3792]
	movs	r1, #20
	ldrh	fp, [r4, #34]
.LVL1421:
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	.loc 3 851 0
	ldr	r2, [sp, #12]
.LVL1422:
	cmp	r5, r2
	bne	.L1624
	.loc 3 852 0
	ldr	r2, [r4, #3776]
.LVL1423:
	.loc 3 853 0
	mov	r1, fp
	mov	r0, r7
	.loc 3 852 0
	str	r3, [sp, #32]
	str	r2, [r3, #8]
	.loc 3 853 0
	bl	__aeabi_uidivmod
.LVL1424:
	.loc 3 854 0
	sub	r2, fp, r1
	.loc 3 853 0
	str	r1, [sp, #20]
.LVL1425:
	ldr	r1, [sp, #28]
.LVL1426:
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	.loc 3 856 0
	cmp	r3, fp
	str	r3, [sp, #24]
	ldr	r3, [sp, #32]
	bne	.L1625
	.loc 3 857 0
	str	r10, [r3, #8]
.L1625:
	.loc 3 866 0
	ldrh	r2, [r4, #88]
	ldr	r1, [r4, #3772]
.LVL1427:
	.loc 3 867 0
	str	r5, [r3, #16]
	.loc 3 866 0
	muls	r2, r6, r2
	.loc 3 868 0
	adds	r6, r6, #1
	.loc 3 866 0
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1623
.LVL1428:
.L1624:
	.loc 3 858 0
	ldr	r2, [sp, #16]
.LVL1429:
	cmp	r5, r2
	bne	.L1626
	.loc 3 859 0
	ldr	r2, [r4, #3768]
.LVL1430:
	.loc 3 860 0
	ldr	r1, [sp, #8]
	.loc 3 859 0
	str	r2, [r3, #8]
	.loc 3 860 0
	mul	r2, fp, r5
	sub	r9, r1, r2
	.loc 3 861 0
	cmp	fp, r9
	bne	.L1625
.L1645:
	.loc 3 864 0
	subs	r2, r2, r7
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1625
.LVL1431:
.L1626:
	mul	r2, r5, fp
.LVL1432:
	b	.L1645
.LVL1433:
.L1629:
	.loc 3 880 0
	ldr	r0, [sp, #16]
.LVL1434:
	cmp	r0, r1
	bne	.L1630
	.loc 3 881 0
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3768]
	cmp	r1, r2
	bne	.L1630
	str	r3, [sp, #44]
	.loc 3 882 0
	ldrh	r0, [r4, #34]
.LVL1435:
	ldr	r3, [sp, #16]
.LVL1436:
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r10, r0, lsl #9
	b	.L1646
.LVL1437:
.L1636:
	.loc 3 830 0
	mov	r8, #-1
	b	.L1615
.L1637:
	mov	r8, r3
	b	.L1615
.L1648:
	.align	2
.L1647:
	.word	.LANCHOR0
	.word	.LANCHOR2
.LFE164:
	.fnend
	.size	FtlRead, .-FtlRead
	.align	1
	.global	sftl_read
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_read, %function
sftl_read:
	.fnstart
.LFB169:
	.loc 3 1201 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL1438:
	.loc 3 1202 0
	mov	r3, r2
	mov	r2, r1
.LVL1439:
	mov	r1, r0
.LVL1440:
	movs	r0, #0
.LVL1441:
	b	FtlRead
.LVL1442:
.LFE169:
	.fnend
	.size	sftl_read, .-sftl_read
	.align	1
	.global	FtlWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
.LFB167:
	.loc 3 959 0
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1443:
	.loc 3 967 0
	cmp	r0, #16
	.loc 3 959 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI191:
	.pad #68
	sub	sp, sp, #68
.LCFI192:
	.loc 3 959 0
	mov	r8, r1
	str	r2, [sp, #12]
	str	r3, [sp, #4]
	.loc 3 967 0
	bne	.L1651
	.loc 3 968 0
	mov	r2, r3
.LVL1444:
	ldr	r1, [sp, #12]
	add	r0, r8, #256
.LVL1445:
	bl	FtlVendorPartWrite
.LVL1446:
.L1650:
	.loc 3 1150 0
	add	sp, sp, #68
.LCFI193:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1447:
.L1651:
.LCFI194:
	.loc 3 971 0
	ldr	r3, [sp, #12]
.LVL1448:
	ldr	r4, .L1717
	adds	r7, r1, r3
	ldr	r3, [r4, #116]
.LVL1449:
	cmp	r7, r3
	bhi	.L1693
	.loc 3 971 0 is_stmt 0 discriminator 1
	ldr	r6, .L1717+4
	ldr	r0, [r6]
.LVL1450:
	adds	r5, r0, #1
	beq	.L1650
	.loc 3 974 0 is_stmt 1
	ldr	r3, .L1717+8
	mov	r2, #2048
	.loc 3 975 0
	ldrh	r5, [r4, #34]
	mov	r0, r8
	.loc 3 974 0
	str	r2, [r3, #-220]
	.loc 3 975 0
	mov	r1, r5
	bl	__aeabi_uidiv
.LVL1451:
	.loc 3 976 0
	mov	r1, r5
	.loc 3 975 0
	mov	r10, r0
.LVL1452:
	.loc 3 976 0
	subs	r0, r7, #1
	bl	__aeabi_uidiv
.LVL1453:
	.loc 3 977 0
	sub	r7, r0, r10
	.loc 3 980 0
	ldr	fp, [r4, #3948]
	.loc 3 977 0
	adds	r3, r7, #1
.LVL1454:
	.loc 3 979 0
	adds	r2, r7, #1
.LVL1455:
	.loc 3 977 0
	str	r3, [sp, #8]
	.loc 3 979 0
	ldr	r3, [r4, #2448]
	.loc 3 976 0
	str	r0, [sp, #20]
.LVL1456:
	.loc 3 979 0
	add	r3, r3, r2
	str	r3, [r4, #2448]
	.loc 3 980 0
	cmp	fp, #0
	beq	.L1653
	.loc 3 981 0
	ldr	r3, [fp, #16]
	cmp	r10, r3
	beq	.L1654
	.loc 3 982 0
	bl	FtlCacheWriteBack
.LVL1457:
.L1653:
	.loc 3 1005 0
	ldr	r3, [r6, #8]
	cmp	r3, #0
	beq	.L1695
	.loc 3 1006 0 discriminator 1
	ldr	r5, .L1717+12
	.loc 3 1005 0 discriminator 1
	ldrh	r2, [r4, #2284]
.LVL1458:
	.loc 3 1006 0 discriminator 1
	add	r3, r5, #48
	cmp	r2, #0
	it	eq
	moveq	r5, r3
.LVL1459:
.L1656:
	.loc 3 1009 0
	ldr	r4, .L1717
	.loc 3 1065 0
	mov	r6, r10
.LVL1460:
.L1657:
	.loc 3 1008 0
	ldr	r3, [sp, #8]
.LVL1461:
	cmp	r3, #0
	beq	.L1691
	.loc 3 1009 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #8]
.LVL1462:
	cmp	r2, r3
	bcc	.L1658
	.loc 3 1009 0 is_stmt 0 discriminator 1
	movw	r2, #1009
	ldr	r1, .L1717+16
	ldr	r0, .L1717+20
	bl	printk
.LVL1463:
.L1658:
	.loc 3 1010 0 is_stmt 1
	ldrh	r9, [r5, #4]
.LVL1464:
	cmp	r9, #0
	bne	.L1659
	.loc 3 1011 0
	bl	FtlCacheWriteBack
.LVL1465:
	.loc 3 1013 0
	ldr	r3, .L1717+12
	ldr	r7, .L1717+4
	cmp	r5, r3
	bne	.L1660
	.loc 3 1014 0
	ldrh	r9, [r4, #2332]
	cmp	r9, #0
	bne	.L1661
	.loc 3 1015 0
	add	r0, r5, #48
	bl	allocate_new_data_superblock
.LVL1466:
	.loc 3 1016 0
	str	r9, [r7, #8]
.L1661:
	.loc 3 1020 0
	ldr	r5, .L1717+12
.LVL1467:
	.loc 3 1018 0
	ldr	r0, .L1717+12
	bl	allocate_new_data_superblock
.LVL1468:
	.loc 3 1019 0
	ldr	r2, [r7, #8]
	.loc 3 1020 0
	add	r3, r5, #48
	cmp	r2, #0
	it	ne
	movne	r5, r3
.LVL1469:
.L1662:
	.loc 3 1030 0
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1659
	.loc 3 1031 0
	mov	r0, r5
	bl	allocate_new_data_superblock
.LVL1470:
.L1659:
	.loc 3 1038 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	.loc 3 1040 0
	ldrh	r3, [r5, #4]
	ldr	r1, [sp, #8]
.LVL1471:
	.loc 3 1038 0
	lsls	r2, r2, #2
	cmp	r3, r1
	it	cs
	movcs	r3, r1
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	.loc 3 1042 0
	ldrh	r3, [r4, #8]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1663
	.loc 3 1042 0 is_stmt 0 discriminator 1
	movw	r2, #1042
	ldr	r1, .L1717+16
.LVL1472:
	ldr	r0, .L1717+20
	bl	printk
.LVL1473:
.L1663:
	.loc 3 1065 0 is_stmt 1 discriminator 1
	mov	r9, #0
.LVL1474:
.L1664:
	.loc 3 1043 0 discriminator 1
	ldr	r3, [sp, #36]
	cmp	r9, r3
	bne	.L1685
.L1665:
	.loc 3 1124 0
	ldr	r3, [r4, #3948]
	cmp	r3, #0
	beq	.L1686
	.loc 3 1126 0
	ldr	r3, [sp, #8]
.LVL1475:
	.loc 3 1127 0
	subs	r9, r9, #1
	.loc 3 1126 0
	add	r3, r3, #-1
	str	r3, [sp, #8]
	.loc 3 1127 0
	bne	.L1686
.L1691:
	.loc 3 1134 0
	ldr	r3, [sp, #20]
.LVL1476:
	movs	r0, #0
	.loc 3 1136 0
	ldr	r4, .L1717
	.loc 3 1134 0
	sub	r1, r3, r10
	bl	rk_ftl_garbage_collect
.LVL1477:
	.loc 3 1136 0
	ldrh	r3, [r4, #2276]
	cmp	r3, #15
	bhi	.L1688
	.loc 3 1138 0
	movw	r5, #65535
.LVL1478:
.L1712:
	ldrh	r3, [r4, #2568]
	cmp	r3, r5
	bne	.L1692
	.loc 3 1138 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #3182]
	cmp	r3, r5
	bne	.L1692
	.loc 3 1139 0 is_stmt 1
	movs	r0, #0
	bl	List_get_gc_head_node
.LVL1479:
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.LVL1480:
.L1692:
	.loc 3 1142 0
	movs	r1, #1
	.loc 3 1140 0
	movs	r3, #128
	.loc 3 1142 0
	mov	r0, r1
	.loc 3 1140 0
	strh	r3, [r4, #3130]	@ movhi
	.loc 3 1141 0
	strh	r3, [r4, #3128]	@ movhi
	.loc 3 1142 0
	bl	rk_ftl_garbage_collect
.LVL1481:
	.loc 3 1143 0
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1482:
	.loc 3 1144 0
	ldrh	r3, [r4, #2276]
	cmp	r3, #8
	bls	.L1712
	b	.L1688
.LVL1483:
.L1654:
	.loc 3 984 0
	ldr	r3, [r4, #2452]
	.loc 3 985 0
	mov	r1, r5
	mov	r0, r8
	.loc 3 984 0
	adds	r3, r3, #1
	str	r3, [r4, #2452]
	.loc 3 985 0
	bl	__aeabi_uidivmod
.LVL1484:
	ldr	r2, [sp, #12]
.LVL1485:
	.loc 3 986 0
	subs	r5, r5, r1
	.loc 3 989 0
	ldr	r0, [fp, #8]
	.loc 3 985 0
	mov	r3, r1
	.loc 3 989 0
	ldr	r1, [sp, #4]
	cmp	r5, r2
	it	cs
	movcs	r5, r2
	lsl	r9, r5, #9
	add	r0, r0, r3, lsl #9
	mov	r2, r9
.LVL1486:
	bl	ftl_memcpy
.LVL1487:
	.loc 3 991 0
	cbnz	r7, .L1655
.LVL1488:
.L1688:
	.loc 3 992 0
	movs	r0, #0
	b	.L1650
.LVL1489:
.L1655:
	.loc 3 993 0
	ldr	r3, [sp, #12]
.LVL1490:
	.loc 3 994 0
	add	r8, r8, r5
	.loc 3 996 0
	add	r10, r10, #1
	.loc 3 993 0
	subs	r3, r3, r5
	str	r3, [sp, #12]
	.loc 3 995 0
	ldr	r3, [sp, #4]
.LVL1491:
	add	r3, r3, r9
	str	r3, [sp, #4]
	.loc 3 997 0
	bl	FtlCacheWriteBack
.LVL1492:
	str	r7, [sp, #8]
	b	.L1653
.LVL1493:
.L1695:
	.loc 3 1003 0
	ldr	r5, .L1717+12
.LVL1494:
	b	.L1656
.LVL1495:
.L1660:
	.loc 3 1025 0
	ldrh	r2, [r4, #2284]
	.loc 3 1024 0
	str	r9, [r7, #8]
	.loc 3 1025 0
	cbnz	r2, .L1698
	.loc 3 1026 0
	mov	r0, r5
	bl	allocate_new_data_superblock
.LVL1496:
	b	.L1662
.L1698:
	mov	r5, r3
	b	.L1659
.LVL1497:
.L1685:
	.loc 3 1044 0
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1665
	.loc 3 1048 0
	movs	r2, #0
	add	r1, sp, #40
.LVL1498:
	mov	r0, r6
	movs	r7, #20
	bl	log2phys
.LVL1499:
	.loc 3 1049 0
	mov	r0, r5
	mul	r7, r7, r9
	bl	get_new_active_ppa
.LVL1500:
	.loc 3 1052 0
	ldrh	r2, [r4, #88]
	.loc 3 1051 0
	ldr	r3, [r4, #3800]
	.loc 3 1052 0
	mul	r1, r2, r9
	.loc 3 1051 0
	add	r3, r3, r7
	str	r0, [r3, #4]
	.loc 3 1053 0
	str	r6, [r3, #16]
	.loc 3 1052 0
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3772]
	add	fp, r1, r0
	str	r1, [sp, #32]
	str	fp, [r3, #12]
	.loc 3 1056 0
	movs	r1, #0
	mov	r0, fp
	bl	ftl_memset
.LVL1501:
	.loc 3 1058 0
	cmp	r6, r10
	beq	.L1666
	.loc 3 1058 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #20]
.LVL1502:
	cmp	r6, r3
	bne	.L1711
	.loc 3 1066 0 is_stmt 1
	ldr	r3, [sp, #12]
.LVL1503:
	add	r2, r8, r3
	ldrh	r3, [r4, #34]
.LVL1504:
	smulbb	r3, r3, r6
	subs	r2, r2, r3
	uxth	r3, r2
	str	r3, [sp, #16]
	.loc 3 1065 0
	movs	r3, #0
	str	r3, [sp, #24]
	b	.L1669
.L1666:
	.loc 3 1060 0
	ldrh	r2, [r4, #34]
	mov	r0, r8
	mov	r1, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidivmod
.LVL1505:
	.loc 3 1061 0
	ldr	r2, [sp, #16]
	.loc 3 1060 0
	str	r1, [sp, #24]
	.loc 3 1061 0
	subs	r2, r2, r1
	ldr	r1, [sp, #12]
.LVL1506:
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	str	r3, [sp, #16]
.L1669:
	.loc 3 1069 0
	ldrh	r3, [r4, #34]
	ldr	r2, [sp, #16]
	cmp	r2, r3
	.loc 3 1071 0
	ldr	r3, [r4, #3800]
	.loc 3 1069 0
	bne	.L1670
	.loc 3 1070 0
	cmp	r6, r10
	.loc 3 1071 0
	add	r7, r7, r3
	.loc 3 1070 0
	bne	.L1671
	.loc 3 1071 0
	ldr	r3, [sp, #4]
.LVL1507:
.L1715:
	.loc 3 1114 0
	str	r3, [r7, #8]
.LVL1508:
	b	.L1672
.LVL1509:
.L1671:
	.loc 3 1073 0
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #4]
.LVL1510:
	muls	r2, r6, r2
	sub	r2, r2, r8
	add	r2, r3, r2, lsl #9
	str	r2, [r7, #8]
.LVL1511:
.L1672:
	.loc 3 1116 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #8]
.LVL1512:
	cmp	r2, r3
	bcc	.L1682
	.loc 3 1116 0 is_stmt 0 discriminator 1
	movw	r2, #1116
	ldr	r1, .L1717+16
.LVL1513:
	ldr	r0, .L1717+20
	bl	printk
.LVL1514:
.L1682:
	.loc 3 1117 0 is_stmt 1
	ldr	r2, [sp, #32]
	movw	r3, #61589
	ldr	r1, [sp, #28]
.LVL1515:
	.loc 3 1043 0
	add	r9, r9, #1
	.loc 3 1117 0
	strh	r3, [r2, r1]	@ movhi
	.loc 3 1118 0
	ldr	r3, [r4, #2476]
	.loc 3 1119 0
	str	r6, [fp, #8]
	.loc 3 1122 0
	adds	r6, r6, #1
	.loc 3 1118 0
	str	r3, [fp, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, #0
	str	r3, [r4, #2476]
	.loc 3 1120 0
	ldr	r3, [sp, #40]
.LVL1516:
	str	r3, [fp, #12]
	.loc 3 1121 0
	ldrh	r3, [r5]
.LVL1517:
	strh	r3, [fp, #2]	@ movhi
	b	.L1664
.L1718:
	.align	2
.L1717:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2280
	.word	.LANCHOR1+574
	.word	.LC1
.LVL1518:
.L1670:
	.loc 3 1076 0
	cmp	r6, r10
	.loc 3 1077 0
	add	r3, r3, r7
	ite	eq
	ldreq	r2, [r4, #3776]
	.loc 3 1079 0
	ldrne	r2, [r4, #3768]
	str	r2, [r3, #8]
	.loc 3 1081 0
	ldr	r3, [sp, #40]
	adds	r1, r3, #1
.LVL1519:
	beq	.L1675
.LBB517:
	.loc 3 1083 0
	str	r3, [sp, #48]
	.loc 3 1087 0
	movs	r1, #1
	.loc 3 1085 0
	ldr	r3, [r4, #3800]
	.loc 3 1087 0
	add	r0, sp, #44
	.loc 3 1084 0
	str	r6, [sp, #60]
	.loc 3 1085 0
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	.loc 3 1086 0
	ldr	r3, [r3, #12]
	.loc 3 1085 0
	str	r2, [sp, #52]
	.loc 3 1087 0
	movs	r2, #0
	.loc 3 1086 0
	str	r3, [sp, #56]
	.loc 3 1087 0
	bl	FlashReadPages
.LVL1520:
	.loc 3 1088 0
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1676
	.loc 3 1089 0
	ldr	r3, [r4, #2688]
	adds	r3, r3, #1
	str	r3, [r4, #2688]
.L1679:
	ldr	r3, [sp, #16]
.LBE517:
	.loc 3 1101 0
	cmp	r6, r10
	lsl	r2, r3, #9
	bne	.L1680
	.loc 3 1102 0
	ldr	r3, [r4, #3800]
	ldr	r1, [sp, #4]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #24]
	add	r0, r0, r3, lsl #9
.L1714:
	.loc 3 1104 0
	bl	ftl_memcpy
.LVL1521:
	.loc 3 1106 0
	ldr	r3, [sp, #20]
.LVL1522:
	cmp	r6, r3
	bne	.L1672
	.loc 3 1107 0
	ldrh	r3, [r5, #4]
.LVL1523:
	cmp	r3, #0
	beq	.L1672
	.loc 3 1108 0
	ldr	r3, [r4, #3800]
	add	r7, r7, r3
	.loc 3 1109 0
	ldr	r3, .L1719
	.loc 3 1108 0
	str	r7, [r4, #3948]
	.loc 3 1109 0
	str	r5, [r3, #-236]
	b	.L1672
.L1676:
.LBB518:
	.loc 3 1091 0
	ldr	r3, [fp, #8]
	cmp	r6, r3
	beq	.L1678
	.loc 3 1092 0
	ldr	r3, [r4, #2688]
	.loc 3 1093 0
	mov	r2, r6
	ldr	r0, .L1719+4
	.loc 3 1092 0
	adds	r3, r3, #1
	str	r3, [r4, #2688]
	.loc 3 1093 0
	ldr	r1, [fp, #8]
	bl	printk
.LVL1524:
.L1678:
	.loc 3 1095 0
	ldr	r3, [fp, #8]
	cmp	r6, r3
	beq	.L1679
	.loc 3 1095 0 is_stmt 0 discriminator 1
	movw	r2, #1095
	ldr	r1, .L1719+8
	ldr	r0, .L1719+12
	bl	printk
.LVL1525:
	b	.L1679
.L1675:
.LBE518:
	.loc 3 1098 0 is_stmt 1
	ldr	r3, [r4, #3800]
	movs	r1, #0
	ldrh	r2, [r4, #86]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL1526:
	b	.L1679
.L1680:
	.loc 3 1104 0
	ldrh	r1, [r4, #34]
	ldr	r0, [sp, #4]
.LVL1527:
	ldr	r3, [r4, #3800]
	muls	r1, r6, r1
	add	r3, r3, r7
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
.LVL1528:
	b	.L1714
.LVL1529:
.L1711:
	.loc 3 1114 0
	ldr	r3, [r4, #3800]
.LVL1530:
	ldr	r2, [sp, #4]
.LVL1531:
	add	r7, r7, r3
	ldrh	r3, [r4, #34]
	muls	r3, r6, r3
	sub	r3, r3, r8
	add	r3, r2, r3, lsl #9
	b	.L1715
.LVL1532:
.L1686:
	.loc 3 1130 0
	mov	r3, r5
	movs	r2, #0
	mov	r1, r9
.LVL1533:
	ldr	r0, [r4, #3800]
	bl	FtlProgPages
.LVL1534:
	.loc 3 1131 0
	ldr	r3, [sp, #8]
.LVL1535:
	cmp	r3, r9
	bcs	.L1689
	.loc 3 1131 0 is_stmt 0 discriminator 1
	movw	r2, #1131
	ldr	r1, .L1719+8
	ldr	r0, .L1719+12
	bl	printk
.LVL1536:
.L1689:
	.loc 3 1132 0 is_stmt 1
	ldr	r3, [sp, #8]
	sub	r3, r3, r9
	str	r3, [sp, #8]
	b	.L1657
.LVL1537:
.L1693:
	.loc 3 972 0
	mov	r0, #-1
.LVL1538:
	b	.L1650
.L1720:
	.align	2
.L1719:
	.word	.LANCHOR3
	.word	.LC101
	.word	.LANCHOR1+574
	.word	.LC1
.LFE167:
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	1
	.global	sftl_write
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
	.fnstart
.LFB170:
	.loc 3 1207 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL1539:
	.loc 3 1208 0
	mov	r3, r2
	mov	r2, r1
.LVL1540:
	mov	r1, r0
.LVL1541:
	movs	r0, #0
.LVL1542:
	b	FtlWrite
.LVL1543:
.LFE170:
	.fnend
	.size	sftl_write, .-sftl_write
	.align	1
	.global	FtlLoadSysInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
.LFB224:
	.loc 2 1360 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI195:
	.loc 2 1368 0
	movs	r1, #0
	.loc 2 1365 0
	ldr	r4, .L1743
	ldr	r3, [r4, #2244]
	.loc 2 1368 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #2260]
	.loc 2 1365 0
	str	r3, [r4, #3232]
	.loc 2 1366 0
	ldr	r3, [r4, #3244]
	.loc 2 1368 0
	lsls	r2, r2, #1
	.loc 2 1366 0
	str	r3, [r4, #3236]
	.loc 2 1368 0
	bl	ftl_memset
.LVL1544:
	.loc 2 1369 0
	ldrh	r0, [r4, #2556]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1723
.LVL1545:
.L1731:
	.loc 2 1370 0
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.LVL1546:
.L1723:
	.loc 2 1381 0
	ldr	r7, .L1743+4
	.loc 2 1372 0
	movs	r1, #1
	.loc 2 1378 0
	addw	r6, r4, #3224
	.loc 2 1372 0
	bl	FtlGetLastWrittenPage
.LVL1547:
	sxth	r5, r0
.LVL1548:
	.loc 2 1373 0
	adds	r0, r0, #1
	strh	r0, [r4, #2558]	@ movhi
.L1725:
	.loc 2 1375 0
	cmp	r5, #0
	bge	.L1728
	.loc 2 1388 0
	movw	r2, #1388
	ldr	r1, .L1743+8
	ldr	r0, .L1743+12
	bl	printk
.LVL1549:
.L1727:
	.loc 2 1390 0
	ldrh	r3, [r4, #16]
	ldrh	r2, [r4, #86]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1730
	.loc 2 1390 0 is_stmt 0 discriminator 1
	movw	r2, #1390
	ldr	r1, .L1743+8
	ldr	r0, .L1743+12
	bl	printk
.LVL1550:
.L1730:
	.loc 2 1392 0 is_stmt 1
	movs	r2, #48
	ldr	r1, [r4, #3232]
	ldr	r0, .L1743+16
	bl	ftl_memcpy
.LVL1551:
	.loc 2 1393 0
	ldrh	r2, [r4, #16]
	ldr	r1, [r4, #3232]
	ldr	r0, [r4, #2260]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
.LVL1552:
	.loc 2 1394 0
	ldrh	r1, [r4, #16]
	ldr	r3, [r4, #3232]
	ldr	r0, [r4]
	lsrs	r2, r1, #3
	adds	r1, r1, #24
	lsls	r1, r1, #1
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
.LVL1553:
	.loc 2 1396 0
	ldr	r2, [r4, #2504]
	ldr	r3, .L1743+4
	cmp	r2, r3
	bne	.L1731
	.loc 2 1400 0
	ldrb	r2, [r4, #2514]	@ zero_extendqisi2
	ldrh	r3, [r4, #30]
	.loc 2 1399 0
	ldrh	r5, [r4, #2512]
.LVL1554:
	.loc 2 1400 0
	cmp	r2, r3
	.loc 2 1399 0
	strh	r5, [r4, #2562]	@ movhi
	.loc 2 1400 0
	bne	.L1731
	.loc 2 1403 0
	ldr	r3, .L1743+20
	.loc 2 1405 0
	ldrh	r2, [r4, #34]
	.loc 2 1406 0
	ldr	r6, [r4, #20]
	.loc 2 1403 0
	str	r5, [r3, #-216]
	.loc 2 1404 0
	ldrh	r3, [r4, #78]
	.loc 2 1406 0
	ldrh	r0, [r4, #134]
	ldrh	r1, [r4, #8]
	.loc 2 1404 0
	muls	r3, r5, r3
	.loc 2 1406 0
	subs	r0, r6, r0
	subs	r0, r0, r5
	.loc 2 1404 0
	str	r3, [r4, #2436]
	.loc 2 1405 0
	muls	r3, r2, r3
	str	r3, [r4, #116]
	.loc 2 1406 0
	bl	__aeabi_uidiv
.LVL1555:
	.loc 2 1412 0
	cmp	r5, r6
	.loc 2 1406 0
	strh	r0, [r4, #2552]	@ movhi
	.loc 2 1412 0
	bls	.L1732
	.loc 2 1412 0 is_stmt 0 discriminator 1
	movw	r2, #1412
	ldr	r1, .L1743+8
	ldr	r0, .L1743+12
	bl	printk
.LVL1556:
.L1732:
	.loc 2 1415 0 is_stmt 1
	ldrh	r3, [r4, #2520]
	.loc 2 1414 0
	ldrh	r1, [r4, #2518]
	.loc 2 1415 0
	lsrs	r2, r3, #6
	.loc 2 1416 0
	and	r3, r3, #63
	strb	r3, [r4, #2286]
	.loc 2 1415 0
	strh	r2, [r4, #2282]	@ movhi
	.loc 2 1424 0
	ldrh	r2, [r4, #2522]
	.loc 2 1417 0
	ldrb	r3, [r4, #2515]	@ zero_extendqisi2
	.loc 2 1414 0
	strh	r1, [r4, #2280]	@ movhi
	.loc 2 1424 0
	strh	r2, [r4, #2328]	@ movhi
	.loc 2 1425 0
	ldrh	r2, [r4, #2524]
	.loc 2 1417 0
	strb	r3, [r4, #2288]
	.loc 2 1419 0
	movw	r3, #65535
	strh	r3, [r4, #2568]	@ movhi
	.loc 2 1420 0
	movs	r3, #0
	strh	r3, [r4, #2570]	@ movhi
	.loc 2 1425 0
	lsrs	r0, r2, #6
	.loc 2 1426 0
	and	r2, r2, #63
	strb	r2, [r4, #2334]
	.loc 2 1427 0
	ldrb	r2, [r4, #2516]	@ zero_extendqisi2
	.loc 2 1425 0
	strh	r0, [r4, #2330]	@ movhi
	.loc 2 1421 0
	strb	r3, [r4, #2574]
	.loc 2 1427 0
	strb	r2, [r4, #2336]
	.loc 2 1429 0
	ldrh	r2, [r4, #2526]
	.loc 2 1422 0
	strb	r3, [r4, #2576]
	.loc 2 1434 0
	str	r3, [r4, #2460]
	.loc 2 1429 0
	strh	r2, [r4, #2376]	@ movhi
	.loc 2 1430 0
	ldrh	r2, [r4, #2528]
	.loc 2 1435 0
	str	r3, [r4, #2448]
	.loc 2 1436 0
	str	r3, [r4, #2440]
	.loc 2 1437 0
	str	r3, [r4, #2456]
	.loc 2 1430 0
	lsrs	r0, r2, #6
	.loc 2 1431 0
	and	r2, r2, #63
	strb	r2, [r4, #2382]
	.loc 2 1432 0
	ldrb	r2, [r4, #2517]	@ zero_extendqisi2
	.loc 2 1440 0
	str	r3, [r4, #2484]
	.loc 2 1430 0
	strh	r0, [r4, #2378]	@ movhi
	.loc 2 1432 0
	strb	r2, [r4, #2384]
	.loc 2 1439 0
	ldr	r2, [r4, #2536]
	str	r2, [r4, #2480]
	.loc 2 1441 0
	str	r3, [r4, #2496]
	.loc 2 1444 0
	ldr	r2, [r4, #2472]
	.loc 2 1442 0
	str	r3, [r4, #2452]
	.loc 2 1444 0
	ldr	r3, [r4, #2544]
	cmp	r3, r2
	.loc 2 1447 0
	ldr	r2, [r4, #2476]
	.loc 2 1445 0
	it	hi
	strhi	r3, [r4, #2472]
	.loc 2 1447 0
	ldr	r3, [r4, #2540]
	cmp	r3, r2
	.loc 2 1448 0
	it	hi
	strhi	r3, [r4, #2476]
	.loc 2 1450 0
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1735
	.loc 2 1451 0
	ldr	r0, .L1743+24
	bl	make_superblock
.LVL1557:
.L1735:
	.loc 2 1453 0
	ldrh	r2, [r4, #2328]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1736
	.loc 2 1454 0
	ldr	r0, .L1743+28
	bl	make_superblock
.LVL1558:
.L1736:
	.loc 2 1456 0
	ldrh	r2, [r4, #2376]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1737
	.loc 2 1457 0
	ldr	r0, .L1743+32
	bl	make_superblock
.LVL1559:
.L1737:
	.loc 2 1459 0
	ldrh	r2, [r4, #2568]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1738
	.loc 2 1460 0
	ldr	r0, .L1743+36
	bl	make_superblock
.LVL1560:
.L1738:
	.loc 2 1462 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL1561:
.L1728:
	.loc 2 1376 0
	ldrh	r3, [r4, #2556]
	.loc 2 1378 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r6
	.loc 2 1376 0
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3228]
	.loc 2 1377 0
	ldr	r3, [r4, #2244]
	str	r3, [r4, #3232]
	.loc 2 1378 0
	bl	FlashReadPages
.LVL1562:
	.loc 2 1381 0
	ldr	r3, [r4, #3224]
	adds	r3, r3, #1
	beq	.L1726
	.loc 2 1382 0 discriminator 1
	ldr	r3, [r4, #2244]
	.loc 2 1381 0 discriminator 1
	ldr	r3, [r3]
	cmp	r3, r7
	bne	.L1726
	.loc 2 1383 0
	ldr	r3, [r4, #3244]
	ldrh	r2, [r3]
	.loc 2 1382 0
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1727
.L1726:
	subs	r5, r5, #1
.LVL1563:
	sxth	r5, r5
.LVL1564:
	b	.L1725
.L1744:
	.align	2
.L1743:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR1+583
	.word	.LC1
	.word	.LANCHOR0+2504
	.word	.LANCHOR3
	.word	.LANCHOR0+2280
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2376
	.word	.LANCHOR0+2568
.LFE224:
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FtlSysBlkInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
.LFB242:
	.loc 2 2306 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI196:
	.loc 2 2310 0
	movs	r3, #0
	ldr	r4, .L1761
	.loc 2 2312 0
	ldrh	r0, [r4, #12]
	.loc 2 2310 0
	strh	r3, [r4, #3968]	@ movhi
	.loc 2 2312 0
	bl	FtlFreeSysBlkQueueInit
.LVL1565:
	.loc 2 2313 0
	bl	FtlScanSysBlk
.LVL1566:
	.loc 2 2314 0
	ldrh	r2, [r4, #2556]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1746
.L1748:
	.loc 2 2315 0
	mov	r5, #-1
.L1745:
	.loc 2 2398 0
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1746:
	.loc 2 2317 0
	bl	FtlLoadSysInfo
.LVL1567:
	mov	r5, r0
	cmp	r0, #0
	bne	.L1748
	.loc 2 2320 0
	bl	FtlLoadMapInfo
.LVL1568:
	.loc 2 2321 0
	bl	FtlLoadVonderInfo
.LVL1569:
	.loc 2 2322 0
	bl	Ftl_load_ext_data
.LVL1570:
	.loc 2 2324 0
	bl	FtlLoadEctTbl
.LVL1571:
	.loc 2 2325 0
	bl	FtlFreeSysBLkSort
.LVL1572:
	.loc 2 2327 0
	bl	SupperBlkListInit
.LVL1573:
	.loc 2 2328 0
	bl	FtlPowerLostRecovery
.LVL1574:
	.loc 2 2330 0
	movs	r0, #1
	bl	FtlUpdateVaildLpn
.LVL1575:
.LBB522:
.LBB523:
	.loc 2 2332 0
	ldrh	r1, [r4, #114]
	mov	r3, r5
.LVL1576:
	.loc 2 2333 0
	ldr	r2, [r4, #2424]
	movs	r0, #12
.L1749:
	.loc 2 2332 0
	cmp	r3, r1
	bge	.L1754
	.loc 2 2333 0
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1750
.L1754:
	.loc 2 2336 0
	ldrh	r2, [r4, #2532]
	.loc 2 2338 0
	cmp	r3, r1
	.loc 2 2336 0
	add	r2, r2, #1
	strh	r2, [r4, #2532]	@ movhi
	.loc 2 2338 0
	bge	.L1760
.L1751:
	.loc 2 2339 0
	ldr	r0, .L1761+4
	bl	FtlSuperblockPowerLostFix
.LVL1577:
	.loc 2 2340 0
	ldr	r0, .L1761+8
	bl	FtlSuperblockPowerLostFix
.LVL1578:
.LBB524:
	.loc 2 2346 0
	ldrh	r1, [r4, #2280]
	ldr	r2, [r4, #2260]
	ldrh	r0, [r4, #2284]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 2 2347 0
	ldrh	r3, [r4, #78]
	.loc 2 2352 0
	ldr	r1, [r4, #2260]
	ldrh	r0, [r4, #2328]
	.loc 2 2347 0
	strh	r3, [r4, #2282]	@ movhi
	.loc 2 2348 0
	movs	r3, #0
	strb	r3, [r4, #2286]
	.loc 2 2349 0
	strh	r3, [r4, #2284]	@ movhi
	.loc 2 2352 0
	ldrh	r6, [r4, #2332]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	.loc 2 2353 0
	ldrh	r2, [r4, #78]
.LBE524:
	.loc 2 2359 0
	ldr	r0, .L1761+12
.LBB525:
	.loc 2 2354 0
	strb	r3, [r4, #2334]
	.loc 2 2353 0
	strh	r2, [r4, #2330]	@ movhi
	.loc 2 2355 0
	strh	r3, [r4, #2332]	@ movhi
.LBE525:
	.loc 2 2359 0
	bl	FtlMapBlkWriteDump_data
.LVL1579:
	.loc 2 2360 0
	ldr	r0, .L1761+16
	bl	FtlMapBlkWriteDump_data
.LVL1580:
	.loc 2 2372 0
	ldrh	r3, [r4, #2534]
	adds	r3, r3, #1
	strh	r3, [r4, #2534]	@ movhi
	.loc 2 2373 0
	bl	l2p_flush
.LVL1581:
	.loc 2 2374 0
	bl	FtlVpcTblFlush
.LVL1582:
	.loc 2 2375 0
	bl	FtlVpcTblFlush
.LVL1583:
	b	.L1755
.LVL1584:
.L1750:
	.loc 2 2332 0
	adds	r3, r3, #1
	b	.L1749
.L1760:
	.loc 2 2338 0
	ldrh	r3, [r4, #3968]
.LVL1585:
	cmp	r3, #0
	bne	.L1751
.L1755:
	.loc 2 2379 0
	ldrh	r0, [r4, #2280]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1756
	ldrh	r3, [r4, #2284]
	cbnz	r3, .L1756
	.loc 2 2380 0
	ldrh	r3, [r4, #2332]
	cbnz	r3, .L1756
	.loc 2 2382 0
	bl	FtlGcRefreshBlock
.LVL1586:
	.loc 2 2383 0
	ldrh	r0, [r4, #2328]
	bl	FtlGcRefreshBlock
.LVL1587:
	.loc 2 2384 0
	ldr	r0, .L1761+4
	bl	allocate_new_data_superblock
.LVL1588:
	.loc 2 2385 0
	ldr	r0, .L1761+8
	bl	allocate_new_data_superblock
.LVL1589:
.L1756:
	.loc 2 2390 0
	ldrh	r3, [r4, #2532]
	lsls	r3, r3, #27
	bne	.L1745
	.loc 2 2391 0
	bl	FtlVpcCheckAndModify
.LVL1590:
	b	.L1745
.L1762:
	.align	2
.L1761:
	.word	.LANCHOR0
	.word	.LANCHOR0+2280
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+3864
	.word	.LANCHOR0+3908
.LBE523:
.LBE522:
.LFE242:
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	1
	.global	ftl_low_format
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
	.fnstart
.LFB160:
	.loc 3 623 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
.LCFI197:
	.loc 3 629 0
	movs	r3, #0
	ldr	r4, .L1787
	.loc 3 631 0
	ldrh	r0, [r4, #12]
	.loc 3 629 0
	str	r3, [r4, #2472]
	.loc 3 630 0
	str	r3, [r4, #2476]
	.loc 3 631 0
	bl	FtlFreeSysBlkQueueInit
.LVL1591:
	.loc 3 632 0
	bl	FtlLoadBbt
.LVL1592:
	cbz	r0, .L1764
	.loc 3 633 0
	bl	FtlMakeBbt
.LVL1593:
.L1764:
	.loc 3 637 0 discriminator 1
	ldr	r0, .L1787+4
	.loc 3 623 0 discriminator 1
	movs	r2, #0
.L1765:
	.loc 3 635 0 discriminator 1
	ldrh	r1, [r4, #34]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1766
	.loc 3 642 0
	ldrh	r6, [r4, #16]
	.loc 3 641 0
	movs	r5, #0
.LVL1594:
.L1767:
	.loc 3 642 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r6
	bhi	.L1768
	.loc 3 645 0
	ldrh	r1, [r4, #8]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1769
.L1773:
	.loc 3 641 0
	movs	r5, #0
.LVL1595:
	mov	r6, r5
.LVL1596:
.L1770:
	.loc 3 656 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1774
	.loc 3 658 0
	ldrh	r3, [r4, #18]
	.loc 3 661 0
	ldrh	r5, [r4, #8]
	ldr	r9, [r4, #20]
	.loc 3 658 0
	str	r3, [r4, #3764]
	.loc 3 661 0
	mov	r1, r5
	mov	r0, r9
	bl	__aeabi_uidiv
.LVL1597:
	.loc 3 662 0
	ubfx	r7, r0, #5, #16
	.loc 3 661 0
	mov	r8, r0
	.loc 3 662 0
	add	r3, r7, #36
	.loc 3 661 0
	str	r0, [r4, #2436]
	.loc 3 662 0
	strh	r3, [r4, #2552]	@ movhi
	.loc 3 663 0
	movs	r3, #24
	muls	r3, r5, r3
	cmp	r6, r3
	ble	.L1775
	.loc 3 664 0
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
.LVL1598:
	str	r0, [r4, #2436]
	.loc 3 665 0
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2552]	@ movhi
.L1775:
	.loc 3 668 0
	ldrh	r3, [r4, #70]
	cbz	r3, .L1777
	.loc 3 669 0
	ldrh	r2, [r4, #2552]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #2552]	@ movhi
	.loc 3 670 0
	mul	r2, r5, r3
	cmp	r6, r2
	.loc 3 672 0
	itttt	lt
	addlt	r3, r3, #32
	.loc 3 671 0
	strlt	r8, [r4, #2436]
	.loc 3 672 0
	addlt	r3, r3, r7
	strhlt	r3, [r4, #2552]	@ movhi
.L1777:
	.loc 3 677 0
	ldrh	r2, [r4, #2552]
	.loc 3 689 0
	movw	r6, #65535
.LVL1599:
	.loc 3 677 0
	ldr	r3, [r4, #2436]
	ldr	r7, .L1787+8
	subs	r3, r3, r2
	muls	r5, r3, r5
	.loc 3 678 0
	ldrh	r3, [r4, #78]
	.loc 3 677 0
	str	r5, [r7, #-216]
	.loc 3 678 0
	muls	r5, r3, r5
	.loc 3 679 0
	ldrh	r3, [r4, #34]
	.loc 3 678 0
	str	r5, [r4, #2436]
	.loc 3 679 0
	muls	r5, r3, r5
	str	r5, [r4, #116]
	mov	r5, r7
	.loc 3 684 0
	bl	FtlBbmTblFlush
.LVL1600:
	.loc 3 687 0
	ldrh	r2, [r4, #18]
	movs	r1, #0
	ldr	r0, [r4, #2260]
	.loc 3 701 0
	ldr	r7, .L1787+12
	.loc 3 687 0
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL1601:
	.loc 3 698 0
	ldrh	r2, [r4, #16]
	.loc 3 688 0
	movs	r3, #0
	str	r3, [r4, #2432]
	.loc 3 698 0
	movs	r1, #255
	.loc 3 690 0
	strh	r3, [r4, #2570]	@ movhi
	.loc 3 691 0
	strb	r3, [r4, #2574]
	.loc 3 692 0
	strb	r3, [r4, #2576]
	.loc 3 698 0
	lsrs	r2, r2, #3
	.loc 3 694 0
	strh	r3, [r4, #2282]	@ movhi
	.loc 3 695 0
	strb	r3, [r4, #2286]
	.loc 3 696 0
	strh	r3, [r4, #2280]	@ movhi
	.loc 3 697 0
	movs	r3, #1
	.loc 3 698 0
	ldr	r0, [r4]
	.loc 3 689 0
	strh	r6, [r4, #2568]	@ movhi
	.loc 3 697 0
	strb	r3, [r4, #2288]
	.loc 3 698 0
	bl	ftl_memset
.LVL1602:
.L1779:
	.loc 3 701 0
	mov	r0, r7
	bl	make_superblock
.LVL1603:
	.loc 3 702 0
	ldrb	r3, [r4, #2287]	@ zero_extendqisi2
	ldrh	r2, [r4, #2280]
	cmp	r3, #0
	bne	.L1780
	.loc 3 705 0
	ldr	r3, [r4, #2260]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	.loc 3 706 0
	ldrh	r3, [r4, #2280]
	adds	r3, r3, #1
	strh	r3, [r4, #2280]	@ movhi
	.loc 3 707 0
	b	.L1779
.L1766:
	.loc 3 636 0 discriminator 3
	ldr	r5, [r4, #3776]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	.loc 3 637 0 discriminator 3
	ldr	r1, [r4, #3768]
	str	r0, [r1, r3, lsl #2]
	b	.L1765
.LVL1604:
.L1768:
	.loc 3 643 0 discriminator 3
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
.LVL1605:
	.loc 3 642 0 discriminator 3
	adds	r6, r6, #1
	.loc 3 643 0 discriminator 3
	add	r5, r5, r0
.LVL1606:
	uxth	r5, r5
.LVL1607:
	.loc 3 642 0 discriminator 3
	uxth	r6, r6
	b	.L1767
.L1769:
	.loc 3 647 0
	mov	r0, r5
	bl	__aeabi_uidiv
.LVL1608:
	ldr	r3, [r4, #108]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
.LVL1609:
	.loc 3 648 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1610:
	.loc 3 650 0
	ldrh	r5, [r4, #16]
.LVL1611:
.L1771:
	.loc 3 650 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r5
	bls	.L1773
	.loc 3 651 0 is_stmt 1 discriminator 3
	mov	r0, r5
	movs	r1, #1
	.loc 3 650 0 discriminator 3
	adds	r5, r5, #1
	.loc 3 651 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1612:
	.loc 3 650 0 discriminator 3
	uxth	r5, r5
	b	.L1771
.LVL1613:
.L1774:
	.loc 3 657 0 discriminator 3
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
.LVL1614:
	add	r6, r6, r0
.LVL1615:
	uxth	r6, r6
.LVL1616:
	b	.L1770
.LVL1617:
.L1780:
	.loc 3 709 0
	ldr	r3, [r4, #2472]
	.loc 3 721 0
	movw	r6, #65535
	.loc 3 710 0
	ldrh	r1, [r4, #2284]
	.loc 3 717 0
	ldr	r7, .L1787+16
	.loc 3 709 0
	str	r3, [r4, #2292]
	adds	r3, r3, #1
	str	r3, [r4, #2472]
	.loc 3 710 0
	ldr	r3, [r4, #2260]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	.loc 3 712 0
	movs	r3, #0
	strh	r3, [r4, #2330]	@ movhi
	.loc 3 713 0
	strb	r3, [r4, #2334]
	.loc 3 714 0
	ldrh	r3, [r4, #2280]
	adds	r3, r3, #1
	strh	r3, [r4, #2328]	@ movhi
	.loc 3 715 0
	movs	r3, #1
	strb	r3, [r4, #2336]
.L1781:
	.loc 3 717 0
	mov	r0, r7
	bl	make_superblock
.LVL1618:
	.loc 3 718 0
	ldrb	r3, [r4, #2335]	@ zero_extendqisi2
	ldrh	r2, [r4, #2328]
	cbnz	r3, .L1782
	.loc 3 721 0
	ldr	r3, [r4, #2260]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	.loc 3 722 0
	ldrh	r3, [r4, #2328]
	adds	r3, r3, #1
	strh	r3, [r4, #2328]	@ movhi
	.loc 3 723 0
	b	.L1781
.L1782:
	.loc 3 725 0
	ldr	r3, [r4, #2472]
	.loc 3 727 0
	movw	r6, #65535
	.loc 3 726 0
	ldrh	r1, [r4, #2332]
	.loc 3 725 0
	str	r3, [r4, #2340]
	adds	r3, r3, #1
	str	r3, [r4, #2472]
	.loc 3 726 0
	ldr	r3, [r4, #2260]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	.loc 3 727 0
	strh	r6, [r4, #2376]	@ movhi
	.loc 3 730 0
	bl	FtlFreeSysBlkQueueOut
.LVL1619:
	.loc 3 731 0
	movs	r3, #0
	.loc 3 730 0
	strh	r0, [r4, #2556]	@ movhi
	.loc 3 731 0
	strh	r3, [r4, #2558]	@ movhi
	.loc 3 733 0
	ldr	r3, [r5, #-216]
	.loc 3 732 0
	strh	r6, [r4, #2560]	@ movhi
	.loc 3 733 0
	strh	r3, [r4, #2562]	@ movhi
	.loc 3 734 0
	ldr	r3, [r4, #2472]
	str	r3, [r4, #2564]
	adds	r3, r3, #1
	str	r3, [r4, #2472]
	.loc 3 735 0
	bl	FtlVpcTblFlush
.LVL1620:
	.loc 3 736 0
	bl	FtlSysBlkInit
.LVL1621:
	cbnz	r0, .L1783
	.loc 3 737 0
	ldr	r3, .L1787+20
	movs	r2, #1
	str	r2, [r3]
.L1783:
	.loc 3 739 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1788:
	.align	2
.L1787:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR3
	.word	.LANCHOR0+2280
	.word	.LANCHOR0+2328
	.word	.LANCHOR2
.LFE160:
	.fnend
	.size	ftl_low_format, .-ftl_low_format
	.align	1
	.global	sftl_init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
	.fnstart
.LFB161:
	.loc 3 743 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI198:
	.loc 3 746 0
	mov	r3, #-1
	ldr	r5, .L1795
	.loc 3 753 0
	ldr	r4, .L1795+4
	.loc 3 749 0
	ldr	r1, .L1795+8
	ldr	r0, .L1795+12
	.loc 3 746 0
	str	r3, [r5]
	.loc 3 749 0
	bl	printk
.LVL1622:
	.loc 3 750 0
	ldr	r0, .L1795+16
	bl	FtlConstantsInit
.LVL1623:
	.loc 3 751 0
	bl	FtlMemInit
.LVL1624:
	.loc 3 752 0
	bl	FtlVariablesInit
.LVL1625:
	.loc 3 753 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1626:
	.loc 3 756 0
	bl	FtlLoadBbt
.LVL1627:
	cbnz	r0, .L1793
	.loc 3 767 0
	bl	FtlSysBlkInit
.LVL1628:
	cbnz	r0, .L1793
	.loc 3 779 0
	movs	r3, #1
	str	r3, [r5]
	.loc 3 783 0
	ldrh	r3, [r4, #2276]
	cmp	r3, #15
	bhi	.L1793
	movw	r4, #8129
.L1792:
.LBB526:
	.loc 3 786 0 discriminator 3
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1629:
	.loc 3 785 0 discriminator 3
	subs	r4, r4, #1
	bne	.L1792
.L1793:
.LBE526:
	.loc 3 794 0
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1796:
	.align	2
.L1795:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC70
	.word	.LC71
	.word	g_nand_phy_info
.LFE161:
	.fnend
	.size	sftl_init, .-sftl_init
	.align	1
	.global	ftl_memcmp
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_memcmp, %function
ftl_memcmp:
	.fnstart
.LFB279:
	.loc 1 211 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL1630:
	.loc 1 212 0
	b	memcmp
.LVL1631:
.LFE279:
	.fnend
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
	.set	.LANCHOR1,. + 0
	.type	__func__.6674, %object
	.size	__func__.6674, 17
__func__.6674:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6669, %object
	.size	__func__.6669, 17
__func__.6669:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6704, %object
	.size	__func__.6704, 17
__func__.6704:
	.ascii	"List_remove_node\000"
	.type	__func__.6736, %object
	.size	__func__.6736, 22
__func__.6736:
	.ascii	"List_update_data_list\000"
	.type	__func__.6777, %object
	.size	__func__.6777, 26
__func__.6777:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6841, %object
	.size	__func__.6841, 22
__func__.6841:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7164, %object
	.size	__func__.7164, 16
__func__.7164:
	.ascii	"make_superblock\000"
	.type	__func__.7338, %object
	.size	__func__.7338, 19
__func__.7338:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.7578, %object
	.size	__func__.7578, 15
__func__.7578:
	.ascii	"FlashReadPages\000"
	.type	__func__.7595, %object
	.size	__func__.7595, 15
__func__.7595:
	.ascii	"FlashProgPages\000"
	.type	__func__.6287, %object
	.size	__func__.6287, 11
__func__.6287:
	.ascii	"FtlMemInit\000"
	.type	__func__.6503, %object
	.size	__func__.6503, 14
__func__.6503:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.6745, %object
	.size	__func__.6745, 16
__func__.6745:
	.ascii	"load_l2p_region\000"
	.type	__func__.6802, %object
	.size	__func__.6802, 31
__func__.6802:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6816, %object
	.size	__func__.6816, 16
__func__.6816:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6788, %object
	.size	__func__.6788, 15
__func__.6788:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6858, %object
	.size	__func__.6858, 9
__func__.6858:
	.ascii	"log2phys\000"
	.type	__func__.7073, %object
	.size	__func__.7073, 16
__func__.7073:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7226, %object
	.size	__func__.7226, 14
__func__.7226:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.6953, %object
	.size	__func__.6953, 14
__func__.6953:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7185, %object
	.size	__func__.7185, 18
__func__.7185:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.7310, %object
	.size	__func__.7310, 25
__func__.7310:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7351, %object
	.size	__func__.7351, 16
__func__.7351:
	.ascii	"update_vpc_list\000"
	.type	__func__.7358, %object
	.size	__func__.7358, 20
__func__.7358:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7107, %object
	.size	__func__.7107, 22
__func__.7107:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.6546, %object
	.size	__func__.6546, 11
__func__.6546:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.6931, %object
	.size	__func__.6931, 15
__func__.6931:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7210, %object
	.size	__func__.7210, 21
__func__.7210:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7331, %object
	.size	__func__.7331, 29
__func__.7331:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6411, %object
	.size	__func__.6411, 13
__func__.6411:
	.ascii	"FtlProgPages\000"
	.type	__func__.7428, %object
	.size	__func__.7428, 19
__func__.7428:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7534, %object
	.size	__func__.7534, 23
__func__.7534:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6439, %object
	.size	__func__.6439, 9
__func__.6439:
	.ascii	"FtlWrite\000"
	.type	__func__.7010, %object
	.size	__func__.7010, 15
__func__.7010:
	.ascii	"FtlLoadSysInfo\000"
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
	.short	-1
	.space	2
	.type	power_up_flag, %object
	.size	power_up_flag, 4
power_up_flag:
	.word	1
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 4344
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
	.space	4
	.type	c_ftl_nand_sys_blks_per_plane, %object
	.size	c_ftl_nand_sys_blks_per_plane, 4
c_ftl_nand_sys_blks_per_plane:
	.space	4
	.type	c_ftl_nand_planes_num, %object
	.size	c_ftl_nand_planes_num, 2
c_ftl_nand_planes_num:
	.space	2
	.space	2
	.type	c_ftl_nand_max_sys_blks, %object
	.size	c_ftl_nand_max_sys_blks, 4
c_ftl_nand_max_sys_blks:
	.space	4
	.type	c_ftl_nand_data_blks_per_plane, %object
	.size	c_ftl_nand_data_blks_per_plane, 2
c_ftl_nand_data_blks_per_plane:
	.space	2
	.type	c_ftl_nand_blk_pre_plane, %object
	.size	c_ftl_nand_blk_pre_plane, 2
c_ftl_nand_blk_pre_plane:
	.space	2
	.type	c_ftl_nand_max_data_blks, %object
	.size	c_ftl_nand_max_data_blks, 4
c_ftl_nand_max_data_blks:
	.space	4
	.type	c_ftl_nand_totle_phy_blks, %object
	.size	c_ftl_nand_totle_phy_blks, 4
c_ftl_nand_totle_phy_blks:
	.space	4
	.type	c_ftl_nand_type, %object
	.size	c_ftl_nand_type, 2
c_ftl_nand_type:
	.space	2
	.type	c_ftl_nand_die_num, %object
	.size	c_ftl_nand_die_num, 2
c_ftl_nand_die_num:
	.space	2
	.type	c_ftl_nand_planes_per_die, %object
	.size	c_ftl_nand_planes_per_die, 2
c_ftl_nand_planes_per_die:
	.space	2
	.type	c_ftl_nand_sec_pre_page, %object
	.size	c_ftl_nand_sec_pre_page, 2
c_ftl_nand_sec_pre_page:
	.space	2
	.type	p_plane_order_table, %object
	.size	p_plane_order_table, 32
p_plane_order_table:
	.space	32
	.type	c_mlc_erase_count_value, %object
	.size	c_mlc_erase_count_value, 2
c_mlc_erase_count_value:
	.space	2
	.type	c_ftl_nand_ext_blk_pre_plane, %object
	.size	c_ftl_nand_ext_blk_pre_plane, 2
c_ftl_nand_ext_blk_pre_plane:
	.space	2
	.type	c_ftl_vendor_part_size, %object
	.size	c_ftl_vendor_part_size, 2
c_ftl_vendor_part_size:
	.space	2
	.type	c_ftl_nand_blks_per_die, %object
	.size	c_ftl_nand_blks_per_die, 2
c_ftl_nand_blks_per_die:
	.space	2
	.type	c_ftl_nand_blks_per_die_shift, %object
	.size	c_ftl_nand_blks_per_die_shift, 2
c_ftl_nand_blks_per_die_shift:
	.space	2
	.type	c_ftl_nand_page_pre_blk, %object
	.size	c_ftl_nand_page_pre_blk, 2
c_ftl_nand_page_pre_blk:
	.space	2
	.type	c_ftl_nand_page_pre_slc_blk, %object
	.size	c_ftl_nand_page_pre_slc_blk, 2
c_ftl_nand_page_pre_slc_blk:
	.space	2
	.type	c_ftl_nand_page_pre_super_blk, %object
	.size	c_ftl_nand_page_pre_super_blk, 2
c_ftl_nand_page_pre_super_blk:
	.space	2
	.type	c_ftl_nand_sec_pre_page_shift, %object
	.size	c_ftl_nand_sec_pre_page_shift, 2
c_ftl_nand_sec_pre_page_shift:
	.space	2
	.type	c_ftl_nand_byte_pre_page, %object
	.size	c_ftl_nand_byte_pre_page, 2
c_ftl_nand_byte_pre_page:
	.space	2
	.type	c_ftl_nand_byte_pre_oob, %object
	.size	c_ftl_nand_byte_pre_oob, 2
c_ftl_nand_byte_pre_oob:
	.space	2
	.type	c_ftl_nand_reserved_blks, %object
	.size	c_ftl_nand_reserved_blks, 2
c_ftl_nand_reserved_blks:
	.space	2
	.type	DeviceCapacity, %object
	.size	DeviceCapacity, 4
DeviceCapacity:
	.space	4
	.type	c_ftl_nand_max_vendor_blks, %object
	.size	c_ftl_nand_max_vendor_blks, 2
c_ftl_nand_max_vendor_blks:
	.space	2
	.type	c_ftl_nand_vendor_region_num, %object
	.size	c_ftl_nand_vendor_region_num, 2
c_ftl_nand_vendor_region_num:
	.space	2
	.type	c_ftl_nand_map_blks_per_plane, %object
	.size	c_ftl_nand_map_blks_per_plane, 2
c_ftl_nand_map_blks_per_plane:
	.space	2
	.space	2
	.type	c_ftl_nand_max_map_blks, %object
	.size	c_ftl_nand_max_map_blks, 4
c_ftl_nand_max_map_blks:
	.space	4
	.type	c_ftl_nand_init_sys_blks_per_plane, %object
	.size	c_ftl_nand_init_sys_blks_per_plane, 4
c_ftl_nand_init_sys_blks_per_plane:
	.space	4
	.type	c_ftl_nand_map_region_num, %object
	.size	c_ftl_nand_map_region_num, 2
c_ftl_nand_map_region_num:
	.space	2
	.type	c_ftl_nand_l2pmap_ram_region_num, %object
	.size	c_ftl_nand_l2pmap_ram_region_num, 2
c_ftl_nand_l2pmap_ram_region_num:
	.space	2
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.space	4
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.space	2
	.space	2
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 4
p_vendor_block_table:
	.space	4
	.type	gBbtInfo, %object
	.size	gBbtInfo, 60
gBbtInfo:
	.space	60
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.space	2056
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
	.space	4
	.type	p_data_block_list_table, %object
	.size	p_data_block_list_table, 4
p_data_block_list_table:
	.space	4
	.type	p_data_block_list_head, %object
	.size	p_data_block_list_head, 4
p_data_block_list_head:
	.space	4
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 4
p_valid_page_count_table:
	.space	4
	.type	p_data_block_list_tail, %object
	.size	p_data_block_list_tail, 4
p_data_block_list_tail:
	.space	4
	.type	g_num_data_superblocks, %object
	.size	g_num_data_superblocks, 2
g_num_data_superblocks:
	.space	2
	.space	2
	.type	p_free_data_block_list_head, %object
	.size	p_free_data_block_list_head, 4
p_free_data_block_list_head:
	.space	4
	.type	g_num_free_superblocks, %object
	.size	g_num_free_superblocks, 2
g_num_free_superblocks:
	.space	2
	.space	2
	.type	g_active_superblock, %object
	.size	g_active_superblock, 48
g_active_superblock:
	.space	48
	.type	g_buffer_superblock, %object
	.size	g_buffer_superblock, 48
g_buffer_superblock:
	.space	48
	.type	g_gc_temp_superblock, %object
	.size	g_gc_temp_superblock, 48
g_gc_temp_superblock:
	.space	48
	.type	p_l2p_ram_map, %object
	.size	p_l2p_ram_map, 4
p_l2p_ram_map:
	.space	4
	.type	g_l2p_last_update_region_id, %object
	.size	g_l2p_last_update_region_id, 2
g_l2p_last_update_region_id:
	.space	2
	.type	FtlUpdateVaildLpnCount, %object
	.size	FtlUpdateVaildLpnCount, 2
FtlUpdateVaildLpnCount:
	.space	2
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.space	4
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
	.space	4
	.type	g_totle_read_page_count, %object
	.size	g_totle_read_page_count, 4
g_totle_read_page_count:
	.space	4
	.type	g_totle_discard_page_count, %object
	.size	g_totle_discard_page_count, 4
g_totle_discard_page_count:
	.space	4
	.type	g_totle_write_page_count, %object
	.size	g_totle_write_page_count, 4
g_totle_write_page_count:
	.space	4
	.type	g_totle_cache_write_count, %object
	.size	g_totle_cache_write_count, 4
g_totle_cache_write_count:
	.space	4
	.type	g_totle_l2p_write_count, %object
	.size	g_totle_l2p_write_count, 4
g_totle_l2p_write_count:
	.space	4
	.type	g_totle_gc_page_count, %object
	.size	g_totle_gc_page_count, 4
g_totle_gc_page_count:
	.space	4
	.type	g_totle_write_sector, %object
	.size	g_totle_write_sector, 4
g_totle_write_sector:
	.space	4
	.type	g_totle_read_sector, %object
	.size	g_totle_read_sector, 4
g_totle_read_sector:
	.space	4
	.type	g_GlobalSysVersion, %object
	.size	g_GlobalSysVersion, 4
g_GlobalSysVersion:
	.space	4
	.type	g_GlobalDataVersion, %object
	.size	g_GlobalDataVersion, 4
g_GlobalDataVersion:
	.space	4
	.type	g_totle_mlc_erase_count, %object
	.size	g_totle_mlc_erase_count, 4
g_totle_mlc_erase_count:
	.space	4
	.type	g_totle_slc_erase_count, %object
	.size	g_totle_slc_erase_count, 4
g_totle_slc_erase_count:
	.space	4
	.type	g_totle_avg_erase_count, %object
	.size	g_totle_avg_erase_count, 4
g_totle_avg_erase_count:
	.space	4
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
	.space	4
	.type	g_max_erase_count, %object
	.size	g_max_erase_count, 4
g_max_erase_count:
	.space	4
	.type	g_min_erase_count, %object
	.size	g_min_erase_count, 4
g_min_erase_count:
	.space	4
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.space	2
	.space	2
	.type	gSysInfo, %object
	.size	gSysInfo, 12
gSysInfo:
	.space	12
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.space	48
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.space	512
	.type	g_gc_free_blk_threshold, %object
	.size	g_gc_free_blk_threshold, 2
g_gc_free_blk_threshold:
	.space	2
	.type	g_gc_merge_free_blk_threshold, %object
	.size	g_gc_merge_free_blk_threshold, 2
g_gc_merge_free_blk_threshold:
	.space	2
	.type	g_gc_skip_write_count, %object
	.size	g_gc_skip_write_count, 4
g_gc_skip_write_count:
	.space	4
	.type	g_gc_blk_index, %object
	.size	g_gc_blk_index, 2
g_gc_blk_index:
	.space	2
	.space	2
	.type	g_in_swl_replace, %object
	.size	g_in_swl_replace, 4
g_in_swl_replace:
	.space	4
	.type	g_gc_num_req, %object
	.size	g_gc_num_req, 4
g_gc_num_req:
	.space	4
	.type	gp_gc_page_buf_info, %object
	.size	gp_gc_page_buf_info, 4
gp_gc_page_buf_info:
	.space	4
	.type	p_gc_data_buf, %object
	.size	p_gc_data_buf, 4
p_gc_data_buf:
	.space	4
	.type	p_gc_spare_buf, %object
	.size	p_gc_spare_buf, 4
p_gc_spare_buf:
	.space	4
	.type	req_gc, %object
	.size	req_gc, 4
req_gc:
	.space	4
	.type	c_gc_page_buf_num, %object
	.size	c_gc_page_buf_num, 4
c_gc_page_buf_num:
	.space	4
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 4
p_gc_blk_tbl:
	.space	4
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.space	2
	.space	2
	.type	p_gc_page_info, %object
	.size	p_gc_page_info, 4
p_gc_page_info:
	.space	4
	.type	g_gc_page_offset, %object
	.size	g_gc_page_offset, 2
g_gc_page_offset:
	.space	2
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.space	2
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.space	2
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.space	2
	.type	g_gc_bad_block_temp_tbl, %object
	.size	g_gc_bad_block_temp_tbl, 34
g_gc_bad_block_temp_tbl:
	.space	34
	.type	g_gc_bad_block_gc_index, %object
	.size	g_gc_bad_block_gc_index, 2
g_gc_bad_block_gc_index:
	.space	2
	.type	req_sys, %object
	.size	req_sys, 20
req_sys:
	.space	20
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 4
p_sys_spare_buf:
	.space	4
	.type	check_spare_buf, %object
	.size	check_spare_buf, 512
check_spare_buf:
	.space	512
	.type	req_erase, %object
	.size	req_erase, 4
req_erase:
	.space	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.space	4
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 4
p_io_data_buf_1:
	.space	4
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 4
p_io_spare_buf:
	.space	4
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 4
p_io_data_buf_0:
	.space	4
	.type	g_in_gc_progress, %object
	.size	g_in_gc_progress, 4
g_in_gc_progress:
	.space	4
	.type	g_gc_head_data_block, %object
	.size	g_gc_head_data_block, 4
g_gc_head_data_block:
	.space	4
	.type	g_gc_head_data_block_count, %object
	.size	g_gc_head_data_block_count, 4
g_gc_head_data_block_count:
	.space	4
	.type	req_read, %object
	.size	req_read, 4
req_read:
	.space	4
	.type	req_gc_dst, %object
	.size	req_gc_dst, 4
req_gc_dst:
	.space	4
	.type	req_prgm, %object
	.size	req_prgm, 4
req_prgm:
	.space	4
	.type	p_sys_data_buf_1, %object
	.size	p_sys_data_buf_1, 4
p_sys_data_buf_1:
	.space	4
	.type	p_vendor_data_buf, %object
	.size	p_vendor_data_buf, 4
p_vendor_data_buf:
	.space	4
	.type	g_ect_tbl_info_size, %object
	.size	g_ect_tbl_info_size, 2
g_ect_tbl_info_size:
	.space	2
	.space	2
	.type	p_swl_mul_table, %object
	.size	p_swl_mul_table, 4
p_swl_mul_table:
	.space	4
	.type	gp_ect_tbl_info, %object
	.size	gp_ect_tbl_info, 4
gp_ect_tbl_info:
	.space	4
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 4
p_valid_page_count_check_table:
	.space	4
	.type	p_map_block_table, %object
	.size	p_map_block_table, 4
p_map_block_table:
	.space	4
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 4
p_map_block_valid_page_count:
	.space	4
	.type	p_vendor_block_valid_page_count, %object
	.size	p_vendor_block_valid_page_count, 4
p_vendor_block_valid_page_count:
	.space	4
	.type	p_vendor_block_ver_table, %object
	.size	p_vendor_block_ver_table, 4
p_vendor_block_ver_table:
	.space	4
	.type	p_vendor_region_ppn_table, %object
	.size	p_vendor_region_ppn_table, 4
p_vendor_region_ppn_table:
	.space	4
	.type	p_map_region_ppn_table, %object
	.size	p_map_region_ppn_table, 4
p_map_region_ppn_table:
	.space	4
	.type	p_map_block_ver_table, %object
	.size	p_map_block_ver_table, 4
p_map_block_ver_table:
	.space	4
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 4
p_l2p_map_buf:
	.space	4
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.space	2
	.space	2
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 40
gL2pMapInfo:
	.space	40
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.space	2
	.space	2
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 40
gVendorBlkInfo:
	.space	40
	.type	g_req_cache, %object
	.size	g_req_cache, 4
g_req_cache:
	.space	4
	.type	g_tmp_data_superblock_id, %object
	.size	g_tmp_data_superblock_id, 2
g_tmp_data_superblock_id:
	.space	2
	.space	2
	.type	g_totle_swl_count, %object
	.size	g_totle_swl_count, 4
g_totle_swl_count:
	.space	4
	.type	ftl_gc_temp_power_lost_recovery_flag, %object
	.size	ftl_gc_temp_power_lost_recovery_flag, 4
ftl_gc_temp_power_lost_recovery_flag:
	.space	4
	.type	g_recovery_page_min_ver, %object
	.size	g_recovery_page_min_ver, 4
g_recovery_page_min_ver:
	.space	4
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.space	2
	.space	2
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
	.space	4
	.type	g_recovery_ppa_tbl, %object
	.size	g_recovery_ppa_tbl, 128
g_recovery_ppa_tbl:
	.space	128
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.space	2
	.space	2
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 4
gp_last_act_superblock:
	.space	4
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.space	4
	.type	gc_ink_free_return_value, %object
	.size	gc_ink_free_return_value, 2
gc_ink_free_return_value:
	.space	2
	.type	g_gc_cur_blk_valid_pages, %object
	.size	g_gc_cur_blk_valid_pages, 2
g_gc_cur_blk_valid_pages:
	.space	2
	.type	g_gc_cur_blk_max_valid_pages, %object
	.size	g_gc_cur_blk_max_valid_pages, 2
g_gc_cur_blk_max_valid_pages:
	.space	2
	.space	2
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.space	4
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
	.type	ftl_temp_buf, %object
	.size	ftl_temp_buf, 4096
ftl_temp_buf:
	.space	4096
	.type	check_buf, %object
	.size	check_buf, 4096
check_buf:
	.space	4096
	.type	check_vpc_table, %object
	.size	check_vpc_table, 16384
check_vpc_table:
	.space	16384
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC1:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC2:
	.ascii	"FLASH INFO:\012\000"
.LC3:
	.ascii	"Device Capacity: %d MB\012\000"
.LC4:
	.ascii	"FTL INFO:\012\000"
.LC5:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC6:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC7:
	.ascii	"read_page_count = 0x%x\012\000"
.LC8:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC9:
	.ascii	"write_page_count = 0x%x\012\000"
.LC10:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC11:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC12:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC13:
	.ascii	"totle_write = %d MB\012\000"
.LC14:
	.ascii	"totle_read = %d MB\012\000"
.LC15:
	.ascii	"GSV = 0x%x\012\000"
.LC16:
	.ascii	"GDV = 0x%x\012\000"
.LC17:
	.ascii	"bad blk num = %d\012\000"
.LC18:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC19:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC20:
	.ascii	"slc_EC = 0x%x\012\000"
.LC21:
	.ascii	"avg_EC = 0x%x\012\000"
.LC22:
	.ascii	"sys_EC = 0x%x\012\000"
.LC23:
	.ascii	"max_EC = 0x%x\012\000"
.LC24:
	.ascii	"min_EC = 0x%x\012\000"
.LC25:
	.ascii	"PLT = 0x%x\012\000"
.LC26:
	.ascii	"POT = 0x%x\012\000"
.LC27:
	.ascii	"MaxSector = 0x%x\012\000"
.LC28:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC29:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC30:
	.ascii	"free sysblock = 0x%x\012\000"
.LC31:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC32:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC33:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC34:
	.ascii	"Sys.id = 0x%x\012\000"
.LC35:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC36:
	.ascii	"ACT.page = 0x%x\012\000"
.LC37:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC38:
	.ascii	"ACT.id = 0x%x\012\000"
.LC39:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC40:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC41:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC42:
	.ascii	"BUF.page = 0x%x\012\000"
.LC43:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC44:
	.ascii	"BUF.id = 0x%x\012\000"
.LC45:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC46:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC47:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC48:
	.ascii	"TMP.page = 0x%x\012\000"
.LC49:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC50:
	.ascii	"TMP.id = 0x%x\012\000"
.LC51:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC52:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC53:
	.ascii	"GC.page = 0x%x\012\000"
.LC54:
	.ascii	"GC.plane = 0x%x\012\000"
.LC55:
	.ascii	"GC.id = 0x%x\012\000"
.LC56:
	.ascii	"GC.mode = 0x%x\012\000"
.LC57:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC58:
	.ascii	"WR_CHK = %x %x %x\012\000"
.LC59:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC60:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC61:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC62:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC63:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC64:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC65:
	.ascii	"free min EC= 0x%x\012\000"
.LC66:
	.ascii	"free max EC= 0x%x\012\000"
.LC67:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC68:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC69:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC70:
	.ascii	"SFTL version: 5.0.43 20180116\000"
.LC71:
	.ascii	"%s\012\000"
.LC72:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC73:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC74:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC75:
	.ascii	"prog read error: = %x\012\000"
.LC76:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC77:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC78:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC79:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC80:
	.ascii	"spare:\000"
.LC81:
	.ascii	"map_ppn:\000"
.LC82:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC83:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC84:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC85:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC86:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC87:
	.ascii	"...%s enter...\012\000"
.LC88:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC89:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC90:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC91:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC92:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC93:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC94:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC95:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC96:
	.ascii	"no ect\000"
.LC97:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC98:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC99:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC100:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC101:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
	.section	.debug_frame,"",%progbits
.Lframe0:
	.4byte	.LECIE0-.LSCIE0
.LSCIE0:
	.4byte	0xffffffff
	.byte	0x3
	.ascii	"\000"
	.uleb128 0x1
	.sleb128 -4
	.uleb128 0xe
	.byte	0xc
	.uleb128 0xd
	.uleb128 0
	.align	2
.LECIE0:
.LSFDE0:
	.4byte	.LEFDE0-.LASFDE0
.LASFDE0:
	.4byte	.Lframe0
	.4byte	.LFB271
	.4byte	.LFE271-.LFB271
	.byte	0x4
	.4byte	.LCFI0-.LFB271
	.byte	0xe
	.uleb128 0x20
	.byte	0x83
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x87
	.uleb128 0x4
	.byte	0x88
	.uleb128 0x3
	.byte	0x89
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE0:
.LSFDE2:
	.4byte	.LEFDE2-.LASFDE2
.LASFDE2:
	.4byte	.Lframe0
	.4byte	.LFB286
	.4byte	.LFE286-.LFB286
	.align	2
.LEFDE2:
.LSFDE4:
	.4byte	.LEFDE4-.LASFDE4
.LASFDE4:
	.4byte	.Lframe0
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.align	2
.LEFDE4:
.LSFDE6:
	.4byte	.LEFDE6-.LASFDE6
.LASFDE6:
	.4byte	.Lframe0
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.align	2
.LEFDE6:
.LSFDE8:
	.4byte	.LEFDE8-.LASFDE8
.LASFDE8:
	.4byte	.Lframe0
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.align	2
.LEFDE8:
.LSFDE10:
	.4byte	.LEFDE10-.LASFDE10
.LASFDE10:
	.4byte	.Lframe0
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.byte	0x4
	.4byte	.LCFI1-.LFB155
	.byte	0xe
	.uleb128 0x28
	.byte	0x83
	.uleb128 0xa
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE10:
.LSFDE12:
	.4byte	.LEFDE12-.LASFDE12
.LASFDE12:
	.4byte	.Lframe0
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.align	2
.LEFDE12:
.LSFDE14:
	.4byte	.LEFDE14-.LASFDE14
.LASFDE14:
	.4byte	.Lframe0
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.align	2
.LEFDE14:
.LSFDE16:
	.4byte	.LEFDE16-.LASFDE16
.LASFDE16:
	.4byte	.Lframe0
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.byte	0x4
	.4byte	.LCFI2-.LFB172
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI3-.LCFI2
	.byte	0xe
	.uleb128 0x14
	.align	2
.LEFDE16:
.LSFDE18:
	.4byte	.LEFDE18-.LASFDE18
.LASFDE18:
	.4byte	.Lframe0
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.byte	0x4
	.4byte	.LCFI4-.LFB173
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE18:
.LSFDE20:
	.4byte	.LEFDE20-.LASFDE20
.LASFDE20:
	.4byte	.Lframe0
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.align	2
.LEFDE20:
.LSFDE22:
	.4byte	.LEFDE22-.LASFDE22
.LASFDE22:
	.4byte	.Lframe0
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.align	2
.LEFDE22:
.LSFDE24:
	.4byte	.LEFDE24-.LASFDE24
.LASFDE24:
	.4byte	.Lframe0
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.byte	0x4
	.4byte	.LCFI5-.LFB178
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE24:
.LSFDE26:
	.4byte	.LEFDE26-.LASFDE26
.LASFDE26:
	.4byte	.Lframe0
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.byte	0x4
	.4byte	.LCFI6-.LFB182
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE26:
.LSFDE28:
	.4byte	.LEFDE28-.LASFDE28
.LASFDE28:
	.4byte	.Lframe0
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.byte	0x4
	.4byte	.LCFI7-.LFB183
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE28:
.LSFDE30:
	.4byte	.LEFDE30-.LASFDE30
.LASFDE30:
	.4byte	.Lframe0
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.byte	0x4
	.4byte	.LCFI8-.LFB184
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE30:
.LSFDE32:
	.4byte	.LEFDE32-.LASFDE32
.LASFDE32:
	.4byte	.Lframe0
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.align	2
.LEFDE32:
.LSFDE34:
	.4byte	.LEFDE34-.LASFDE34
.LASFDE34:
	.4byte	.Lframe0
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.byte	0x4
	.4byte	.LCFI9-.LFB187
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE34:
.LSFDE36:
	.4byte	.LEFDE36-.LASFDE36
.LASFDE36:
	.4byte	.Lframe0
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.align	2
.LEFDE36:
.LSFDE38:
	.4byte	.LEFDE38-.LASFDE38
.LASFDE38:
	.4byte	.Lframe0
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.align	2
.LEFDE38:
.LSFDE40:
	.4byte	.LEFDE40-.LASFDE40
.LASFDE40:
	.4byte	.Lframe0
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.byte	0x4
	.4byte	.LCFI10-.LFB191
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE40:
.LSFDE42:
	.4byte	.LEFDE42-.LASFDE42
.LASFDE42:
	.4byte	.Lframe0
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.byte	0x4
	.4byte	.LCFI11-.LFB192
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE42:
.LSFDE44:
	.4byte	.LEFDE44-.LASFDE44
.LASFDE44:
	.4byte	.Lframe0
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.align	2
.LEFDE44:
.LSFDE46:
	.4byte	.LEFDE46-.LASFDE46
.LASFDE46:
	.4byte	.Lframe0
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.byte	0x4
	.4byte	.LCFI12-.LFB196
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI13-.LCFI12
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI14-.LCFI13
	.byte	0xb
	.align	2
.LEFDE46:
.LSFDE48:
	.4byte	.LEFDE48-.LASFDE48
.LASFDE48:
	.4byte	.Lframe0
	.4byte	.LFB195
	.4byte	.LFE195-.LFB195
	.byte	0x4
	.4byte	.LCFI15-.LFB195
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI16-.LCFI15
	.byte	0xa
	.byte	0xce
	.byte	0xc3
	.byte	0xe
	.uleb128 0
	.byte	0x4
	.4byte	.LCFI17-.LCFI16
	.byte	0xb
	.align	2
.LEFDE48:
.LSFDE50:
	.4byte	.LEFDE50-.LASFDE50
.LASFDE50:
	.4byte	.Lframe0
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.byte	0x4
	.4byte	.LCFI18-.LFB197
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE50:
.LSFDE52:
	.4byte	.LEFDE52-.LASFDE52
.LASFDE52:
	.4byte	.Lframe0
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.byte	0x4
	.4byte	.LCFI19-.LFB194
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI20-.LCFI19
	.byte	0xa
	.byte	0xce
	.byte	0xc3
	.byte	0xe
	.uleb128 0
	.byte	0x4
	.4byte	.LCFI21-.LCFI20
	.byte	0xb
	.align	2
.LEFDE52:
.LSFDE54:
	.4byte	.LEFDE54-.LASFDE54
.LASFDE54:
	.4byte	.Lframe0
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.byte	0x4
	.4byte	.LCFI22-.LFB198
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE54:
.LSFDE56:
	.4byte	.LEFDE56-.LASFDE56
.LASFDE56:
	.4byte	.Lframe0
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.byte	0x4
	.4byte	.LCFI23-.LFB199
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE56:
.LSFDE58:
	.4byte	.LEFDE58-.LASFDE58
.LASFDE58:
	.4byte	.Lframe0
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.align	2
.LEFDE58:
.LSFDE60:
	.4byte	.LEFDE60-.LASFDE60
.LASFDE60:
	.4byte	.Lframe0
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.byte	0x4
	.4byte	.LCFI24-.LFB201
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE60:
.LSFDE62:
	.4byte	.LEFDE62-.LASFDE62
.LASFDE62:
	.4byte	.Lframe0
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.byte	0x4
	.4byte	.LCFI25-.LFB202
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE62:
.LSFDE64:
	.4byte	.LEFDE64-.LASFDE64
.LASFDE64:
	.4byte	.Lframe0
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.byte	0x4
	.4byte	.LCFI26-.LFB205
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE64:
.LSFDE66:
	.4byte	.LEFDE66-.LASFDE66
.LASFDE66:
	.4byte	.Lframe0
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.byte	0x4
	.4byte	.LCFI27-.LFB210
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE66:
.LSFDE68:
	.4byte	.LEFDE68-.LASFDE68
.LASFDE68:
	.4byte	.Lframe0
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.byte	0x4
	.4byte	.LCFI28-.LFB216
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE68:
.LSFDE70:
	.4byte	.LEFDE70-.LASFDE70
.LASFDE70:
	.4byte	.Lframe0
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.align	2
.LEFDE70:
.LSFDE72:
	.4byte	.LEFDE72-.LASFDE72
.LASFDE72:
	.4byte	.Lframe0
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.align	2
.LEFDE72:
.LSFDE74:
	.4byte	.LEFDE74-.LASFDE74
.LASFDE74:
	.4byte	.Lframe0
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.byte	0x4
	.4byte	.LCFI29-.LFB229
	.byte	0xe
	.uleb128 0x14
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE74:
.LSFDE76:
	.4byte	.LEFDE76-.LASFDE76
.LASFDE76:
	.4byte	.Lframe0
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.byte	0x4
	.4byte	.LCFI30-.LFB232
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE76:
.LSFDE78:
	.4byte	.LEFDE78-.LASFDE78
.LASFDE78:
	.4byte	.Lframe0
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.byte	0x4
	.4byte	.LCFI31-.LFB234
	.byte	0xe
	.uleb128 0x28
	.byte	0x83
	.uleb128 0xa
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE78:
.LSFDE80:
	.4byte	.LEFDE80-.LASFDE80
.LASFDE80:
	.4byte	.Lframe0
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.byte	0x4
	.4byte	.LCFI32-.LFB243
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE80:
.LSFDE82:
	.4byte	.LEFDE82-.LASFDE82
.LASFDE82:
	.4byte	.Lframe0
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.align	2
.LEFDE82:
.LSFDE84:
	.4byte	.LEFDE84-.LASFDE84
.LASFDE84:
	.4byte	.Lframe0
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.byte	0x4
	.4byte	.LCFI33-.LFB245
	.byte	0xe
	.uleb128 0x14
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE84:
.LSFDE86:
	.4byte	.LEFDE86-.LASFDE86
.LASFDE86:
	.4byte	.Lframe0
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.byte	0x4
	.4byte	.LCFI34-.LFB150
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI35-.LCFI34
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI36-.LCFI35
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI37-.LCFI36
	.byte	0xb
	.align	2
.LEFDE86:
.LSFDE88:
	.4byte	.LEFDE88-.LASFDE88
.LASFDE88:
	.4byte	.Lframe0
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.byte	0x4
	.4byte	.LCFI38-.LFB151
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE88:
.LSFDE90:
	.4byte	.LEFDE90-.LASFDE90
.LASFDE90:
	.4byte	.Lframe0
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.byte	0x4
	.4byte	.LCFI39-.LFB246
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI40-.LCFI39
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI41-.LCFI40
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI42-.LCFI41
	.byte	0xb
	.align	2
.LEFDE90:
.LSFDE92:
	.4byte	.LEFDE92-.LASFDE92
.LASFDE92:
	.4byte	.Lframe0
	.4byte	.LFB247
	.4byte	.LFE247-.LFB247
	.byte	0x4
	.4byte	.LCFI43-.LFB247
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE92:
.LSFDE94:
	.4byte	.LEFDE94-.LASFDE94
.LASFDE94:
	.4byte	.Lframe0
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.byte	0x4
	.4byte	.LCFI44-.LFB250
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE94:
.LSFDE96:
	.4byte	.LEFDE96-.LASFDE96
.LASFDE96:
	.4byte	.Lframe0
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.byte	0x4
	.4byte	.LCFI45-.LFB253
	.byte	0xe
	.uleb128 0x1c
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x87
	.uleb128 0x4
	.byte	0x88
	.uleb128 0x3
	.byte	0x89
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE96:
.LSFDE98:
	.4byte	.LEFDE98-.LASFDE98
.LASFDE98:
	.4byte	.Lframe0
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.byte	0x4
	.4byte	.LCFI46-.LFB254
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE98:
.LSFDE100:
	.4byte	.LEFDE100-.LASFDE100
.LASFDE100:
	.4byte	.Lframe0
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.byte	0x4
	.4byte	.LCFI47-.LFB255
	.byte	0xe
	.uleb128 0x1c
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x87
	.uleb128 0x4
	.byte	0x88
	.uleb128 0x3
	.byte	0x89
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE100:
.LSFDE102:
	.4byte	.LEFDE102-.LASFDE102
.LASFDE102:
	.4byte	.Lframe0
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.align	2
.LEFDE102:
.LSFDE104:
	.4byte	.LEFDE104-.LASFDE104
.LASFDE104:
	.4byte	.Lframe0
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.byte	0x4
	.4byte	.LCFI48-.LFB257
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE104:
.LSFDE106:
	.4byte	.LEFDE106-.LASFDE106
.LASFDE106:
	.4byte	.Lframe0
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.byte	0x4
	.4byte	.LCFI49-.LFB264
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE106:
.LSFDE108:
	.4byte	.LEFDE108-.LASFDE108
.LASFDE108:
	.4byte	.Lframe0
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.byte	0x4
	.4byte	.LCFI50-.LFB265
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE108:
.LSFDE110:
	.4byte	.LEFDE110-.LASFDE110
.LASFDE110:
	.4byte	.Lframe0
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.byte	0x4
	.4byte	.LCFI51-.LFB266
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE110:
.LSFDE112:
	.4byte	.LEFDE112-.LASFDE112
.LASFDE112:
	.4byte	.Lframe0
	.4byte	.LFB272
	.4byte	.LFE272-.LFB272
	.byte	0x4
	.4byte	.LCFI52-.LFB272
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI53-.LCFI52
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI54-.LCFI53
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI55-.LCFI54
	.byte	0xb
	.align	2
.LEFDE112:
.LSFDE114:
	.4byte	.LEFDE114-.LASFDE114
.LASFDE114:
	.4byte	.Lframe0
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.byte	0x4
	.4byte	.LCFI56-.LFB180
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE114:
.LSFDE116:
	.4byte	.LEFDE116-.LASFDE116
.LASFDE116:
	.4byte	.Lframe0
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.byte	0x4
	.4byte	.LCFI57-.LFB186
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI58-.LCFI57
	.byte	0xe
	.uleb128 0x70
	.byte	0x4
	.4byte	.LCFI59-.LCFI58
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI60-.LCFI59
	.byte	0xb
	.align	2
.LEFDE116:
.LSFDE118:
	.4byte	.LEFDE118-.LASFDE118
.LASFDE118:
	.4byte	.Lframe0
	.4byte	.LFB273
	.4byte	.LFE273-.LFB273
	.byte	0x4
	.4byte	.LCFI61-.LFB273
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI62-.LCFI61
	.byte	0xe
	.uleb128 0x50
	.byte	0x4
	.4byte	.LCFI63-.LCFI62
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI64-.LCFI63
	.byte	0xb
	.align	2
.LEFDE118:
.LSFDE120:
	.4byte	.LEFDE120-.LASFDE120
.LASFDE120:
	.4byte	.Lframe0
	.4byte	.LFB274
	.4byte	.LFE274-.LFB274
	.byte	0x4
	.4byte	.LCFI65-.LFB274
	.byte	0xe
	.uleb128 0x28
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x87
	.uleb128 0x4
	.byte	0x88
	.uleb128 0x3
	.byte	0x89
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI66-.LCFI65
	.byte	0xa
	.byte	0xe
	.uleb128 0x1c
	.byte	0x4
	.4byte	.LCFI67-.LCFI66
	.byte	0xb
	.align	2
.LEFDE120:
.LSFDE122:
	.4byte	.LEFDE122-.LASFDE122
.LASFDE122:
	.4byte	.Lframe0
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.byte	0x4
	.4byte	.LCFI68-.LFB190
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE122:
.LSFDE124:
	.4byte	.LEFDE124-.LASFDE124
.LASFDE124:
	.4byte	.Lframe0
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.byte	0x4
	.4byte	.LCFI69-.LFB159
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI70-.LCFI69
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI71-.LCFI70
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI72-.LCFI71
	.byte	0xb
	.align	2
.LEFDE124:
.LSFDE126:
	.4byte	.LEFDE126-.LASFDE126
.LASFDE126:
	.4byte	.Lframe0
	.4byte	.LFB275
	.4byte	.LFE275-.LFB275
	.byte	0x4
	.4byte	.LCFI73-.LFB275
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI74-.LCFI73
	.byte	0xe
	.uleb128 0x68
	.byte	0x4
	.4byte	.LCFI75-.LCFI74
	.byte	0xa
	.byte	0xe
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI76-.LCFI75
	.byte	0xb
	.align	2
.LEFDE126:
.LSFDE128:
	.4byte	.LEFDE128-.LASFDE128
.LASFDE128:
	.4byte	.Lframe0
	.4byte	.LFB276
	.4byte	.LFE276-.LFB276
	.byte	0x4
	.4byte	.LCFI77-.LFB276
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE128:
.LSFDE130:
	.4byte	.LEFDE130-.LASFDE130
.LASFDE130:
	.4byte	.Lframe0
	.4byte	.LFB277
	.4byte	.LFE277-.LFB277
	.byte	0x4
	.4byte	.LCFI78-.LFB277
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE130:
.LSFDE132:
	.4byte	.LEFDE132-.LASFDE132
.LASFDE132:
	.4byte	.Lframe0
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.byte	0x4
	.4byte	.LCFI79-.LFB156
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE132:
.LSFDE134:
	.4byte	.LEFDE134-.LASFDE134
.LASFDE134:
	.4byte	.Lframe0
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.byte	0x4
	.4byte	.LCFI80-.LFB175
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE134:
.LSFDE136:
	.4byte	.LEFDE136-.LASFDE136
.LASFDE136:
	.4byte	.Lframe0
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.byte	0x4
	.4byte	.LCFI81-.LFB203
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE136:
.LSFDE138:
	.4byte	.LEFDE138-.LASFDE138
.LASFDE138:
	.4byte	.Lframe0
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.byte	0x4
	.4byte	.LCFI82-.LFB204
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI83-.LCFI82
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI84-.LCFI83
	.byte	0xb
	.align	2
.LEFDE138:
.LSFDE140:
	.4byte	.LEFDE140-.LASFDE140
.LASFDE140:
	.4byte	.Lframe0
	.4byte	.LFB207
	.4byte	.LFE207-.LFB207
	.byte	0x4
	.4byte	.LCFI85-.LFB207
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE140:
.LSFDE142:
	.4byte	.LEFDE142-.LASFDE142
.LASFDE142:
	.4byte	.Lframe0
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.byte	0x4
	.4byte	.LCFI86-.LFB208
	.byte	0xe
	.uleb128 0x28
	.byte	0x83
	.uleb128 0xa
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE142:
.LSFDE144:
	.4byte	.LEFDE144-.LASFDE144
.LASFDE144:
	.4byte	.Lframe0
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.byte	0x4
	.4byte	.LCFI87-.LFB206
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI88-.LCFI87
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI89-.LCFI88
	.byte	0xb
	.align	2
.LEFDE144:
.LSFDE146:
	.4byte	.LEFDE146-.LASFDE146
.LASFDE146:
	.4byte	.Lframe0
	.4byte	.LFB209
	.4byte	.LFE209-.LFB209
	.byte	0x4
	.4byte	.LCFI90-.LFB209
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE146:
.LSFDE148:
	.4byte	.LEFDE148-.LASFDE148
.LASFDE148:
	.4byte	.Lframe0
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.byte	0x4
	.4byte	.LCFI91-.LFB212
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE148:
.LSFDE150:
	.4byte	.LEFDE150-.LASFDE150
.LASFDE150:
	.4byte	.Lframe0
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.byte	0x4
	.4byte	.LCFI92-.LFB230
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI93-.LCFI92
	.byte	0xe
	.uleb128 0x14
	.align	2
.LEFDE150:
.LSFDE152:
	.4byte	.LEFDE152-.LASFDE152
.LASFDE152:
	.4byte	.Lframe0
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.byte	0x4
	.4byte	.LCFI94-.LFB241
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI95-.LCFI94
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI96-.LCFI95
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI97-.LCFI96
	.byte	0xb
	.align	2
.LEFDE152:
.LSFDE154:
	.4byte	.LEFDE154-.LASFDE154
.LASFDE154:
	.4byte	.Lframe0
	.4byte	.LFB268
	.4byte	.LFE268-.LFB268
	.byte	0x4
	.4byte	.LCFI98-.LFB268
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI99-.LCFI98
	.byte	0xe
	.uleb128 0x68
	.byte	0x4
	.4byte	.LCFI100-.LCFI99
	.byte	0xa
	.byte	0xe
	.uleb128 0x10
	.byte	0x4
	.4byte	.LCFI101-.LCFI100
	.byte	0xb
	.align	2
.LEFDE154:
.LSFDE156:
	.4byte	.LEFDE156-.LASFDE156
.LASFDE156:
	.4byte	.Lframe0
	.4byte	.LFB240
	.4byte	.LFE240-.LFB240
	.byte	0x4
	.4byte	.LCFI102-.LFB240
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI103-.LCFI102
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI104-.LCFI103
	.byte	0xb
	.align	2
.LEFDE156:
.LSFDE158:
	.4byte	.LEFDE158-.LASFDE158
.LASFDE158:
	.4byte	.Lframe0
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.byte	0x4
	.4byte	.LCFI105-.LFB217
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI106-.LCFI105
	.byte	0xa
	.byte	0xce
	.byte	0xc6
	.byte	0xc5
	.byte	0xc4
	.byte	0xe
	.uleb128 0
	.byte	0x4
	.4byte	.LCFI107-.LCFI106
	.byte	0xb
	.align	2
.LEFDE158:
.LSFDE160:
	.4byte	.LEFDE160-.LASFDE160
.LASFDE160:
	.4byte	.Lframe0
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.byte	0x4
	.4byte	.LCFI108-.LFB220
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI109-.LCFI108
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI110-.LCFI109
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI111-.LCFI110
	.byte	0xb
	.align	2
.LEFDE160:
.LSFDE162:
	.4byte	.LEFDE162-.LASFDE162
.LASFDE162:
	.4byte	.Lframe0
	.4byte	.LFB225
	.4byte	.LFE225-.LFB225
	.byte	0x4
	.4byte	.LCFI112-.LFB225
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI113-.LCFI112
	.byte	0xe
	.uleb128 0x40
	.byte	0x4
	.4byte	.LCFI114-.LCFI113
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI115-.LCFI114
	.byte	0xb
	.align	2
.LEFDE162:
.LSFDE164:
	.4byte	.LEFDE164-.LASFDE164
.LASFDE164:
	.4byte	.Lframe0
	.4byte	.LFB226
	.4byte	.LFE226-.LFB226
	.byte	0x4
	.4byte	.LCFI116-.LFB226
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE164:
.LSFDE166:
	.4byte	.LEFDE166-.LASFDE166
.LASFDE166:
	.4byte	.Lframe0
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.byte	0x4
	.4byte	.LCFI117-.LFB227
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE166:
.LSFDE168:
	.4byte	.LEFDE168-.LASFDE168
.LASFDE168:
	.4byte	.Lframe0
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.byte	0x4
	.4byte	.LCFI118-.LFB228
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE168:
.LSFDE170:
	.4byte	.LEFDE170-.LASFDE170
.LASFDE170:
	.4byte	.Lframe0
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.byte	0x4
	.4byte	.LCFI119-.LFB157
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE170:
.LSFDE172:
	.4byte	.LEFDE172-.LASFDE172
.LASFDE172:
	.4byte	.Lframe0
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.byte	0x4
	.4byte	.LCFI120-.LFB235
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI121-.LCFI120
	.byte	0xe
	.uleb128 0x24
	.align	2
.LEFDE172:
.LSFDE174:
	.4byte	.LEFDE174-.LASFDE174
.LASFDE174:
	.4byte	.Lframe0
	.4byte	.LFB259
	.4byte	.LFE259-.LFB259
	.byte	0x4
	.4byte	.LCFI122-.LFB259
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI123-.LCFI122
	.byte	0xce
	.byte	0xc4
	.byte	0xe
	.uleb128 0
	.align	2
.LEFDE174:
.LSFDE176:
	.4byte	.LEFDE176-.LASFDE176
.LASFDE176:
	.4byte	.Lframe0
	.4byte	.LFB260
	.4byte	.LFE260-.LFB260
	.byte	0x4
	.4byte	.LCFI124-.LFB260
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI125-.LCFI124
	.byte	0xe
	.uleb128 0x40
	.byte	0x4
	.4byte	.LCFI126-.LCFI125
	.byte	0xe
	.uleb128 0x24
	.align	2
.LEFDE176:
.LSFDE178:
	.4byte	.LEFDE178-.LASFDE178
.LASFDE178:
	.4byte	.Lframe0
	.4byte	.LFB278
	.4byte	.LFE278-.LFB278
	.align	2
.LEFDE178:
.LSFDE180:
	.4byte	.LEFDE180-.LASFDE180
.LASFDE180:
	.4byte	.Lframe0
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.byte	0x4
	.4byte	.LCFI127-.LFB176
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI128-.LCFI127
	.byte	0xe
	.uleb128 0x24
	.align	2
.LEFDE180:
.LSFDE182:
	.4byte	.LEFDE182-.LASFDE182
.LASFDE182:
	.4byte	.Lframe0
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.byte	0x4
	.4byte	.LCFI129-.LFB248
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI130-.LCFI129
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI131-.LCFI130
	.byte	0xe
	.uleb128 0x24
	.align	2
.LEFDE182:
.LSFDE184:
	.4byte	.LEFDE184-.LASFDE184
.LASFDE184:
	.4byte	.Lframe0
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.byte	0x4
	.4byte	.LCFI132-.LFB267
	.byte	0xe
	.uleb128 0x28
	.byte	0x83
	.uleb128 0xa
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE184:
.LSFDE186:
	.4byte	.LEFDE186-.LASFDE186
.LASFDE186:
	.4byte	.Lframe0
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.byte	0x4
	.4byte	.LCFI133-.LFB251
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE186:
.LSFDE188:
	.4byte	.LEFDE188-.LASFDE188
.LASFDE188:
	.4byte	.Lframe0
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.byte	0x4
	.4byte	.LCFI134-.LFB252
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE188:
.LSFDE190:
	.4byte	.LEFDE190-.LASFDE190
.LASFDE190:
	.4byte	.Lframe0
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.byte	0x4
	.4byte	.LCFI135-.LFB218
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI136-.LCFI135
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI137-.LCFI136
	.byte	0xa
	.byte	0xe
	.uleb128 0x20
	.byte	0x4
	.4byte	.LCFI138-.LCFI137
	.byte	0xb
	.align	2
.LEFDE190:
.LSFDE192:
	.4byte	.LEFDE192-.LASFDE192
.LASFDE192:
	.4byte	.Lframe0
	.4byte	.LFB211
	.4byte	.LFE211-.LFB211
	.byte	0x4
	.4byte	.LCFI139-.LFB211
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE192:
.LSFDE194:
	.4byte	.LEFDE194-.LASFDE194
.LASFDE194:
	.4byte	.Lframe0
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.byte	0x4
	.4byte	.LCFI140-.LFB231
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI141-.LCFI140
	.byte	0xe
	.uleb128 0x58
	.byte	0x4
	.4byte	.LCFI142-.LCFI141
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI143-.LCFI142
	.byte	0xb
	.align	2
.LEFDE194:
.LSFDE196:
	.4byte	.LEFDE196-.LASFDE196
.LASFDE196:
	.4byte	.Lframe0
	.4byte	.LFB238
	.4byte	.LFE238-.LFB238
	.byte	0x4
	.4byte	.LCFI144-.LFB238
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI145-.LCFI144
	.byte	0xe
	.uleb128 0x30
	.byte	0x4
	.4byte	.LCFI146-.LCFI145
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI147-.LCFI146
	.byte	0xb
	.align	2
.LEFDE196:
.LSFDE198:
	.4byte	.LEFDE198-.LASFDE198
.LASFDE198:
	.4byte	.Lframe0
	.4byte	.LFB179
	.4byte	.LFE179-.LFB179
	.byte	0x4
	.4byte	.LCFI148-.LFB179
	.byte	0xe
	.uleb128 0x18
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE198:
.LSFDE200:
	.4byte	.LEFDE200-.LASFDE200
.LASFDE200:
	.4byte	.Lframe0
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.byte	0x4
	.4byte	.LCFI149-.LFB181
	.byte	0xe
	.uleb128 0x28
	.byte	0x83
	.uleb128 0xa
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE200:
.LSFDE202:
	.4byte	.LEFDE202-.LASFDE202
.LASFDE202:
	.4byte	.Lframe0
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.byte	0x4
	.4byte	.LCFI150-.LFB213
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI151-.LCFI150
	.byte	0xe
	.uleb128 0x90
	.byte	0x4
	.4byte	.LCFI152-.LCFI151
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI153-.LCFI152
	.byte	0xb
	.align	2
.LEFDE202:
.LSFDE204:
	.4byte	.LEFDE204-.LASFDE204
.LASFDE204:
	.4byte	.Lframe0
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.align	2
.LEFDE204:
.LSFDE206:
	.4byte	.LEFDE206-.LASFDE206
.LASFDE206:
	.4byte	.Lframe0
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.byte	0x4
	.4byte	.LCFI154-.LFB215
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE206:
.LSFDE208:
	.4byte	.LEFDE208-.LASFDE208
.LASFDE208:
	.4byte	.Lframe0
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.byte	0x4
	.4byte	.LCFI155-.LFB214
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI156-.LCFI155
	.byte	0xe
	.uleb128 0x88
	.byte	0x4
	.4byte	.LCFI157-.LCFI156
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI158-.LCFI157
	.byte	0xb
	.align	2
.LEFDE208:
.LSFDE210:
	.4byte	.LEFDE210-.LASFDE210
.LASFDE210:
	.4byte	.Lframe0
	.4byte	.LFB221
	.4byte	.LFE221-.LFB221
	.byte	0x4
	.4byte	.LCFI159-.LFB221
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE210:
.LSFDE212:
	.4byte	.LEFDE212-.LASFDE212
.LASFDE212:
	.4byte	.Lframe0
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.byte	0x4
	.4byte	.LCFI160-.LFB237
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE212:
.LSFDE214:
	.4byte	.LEFDE214-.LASFDE214
.LASFDE214:
	.4byte	.Lframe0
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.byte	0x4
	.4byte	.LCFI161-.LFB219
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x88
	.uleb128 0x4
	.byte	0x89
	.uleb128 0x3
	.byte	0x8a
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE214:
.LSFDE216:
	.4byte	.LEFDE216-.LASFDE216
.LASFDE216:
	.4byte	.Lframe0
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.byte	0x4
	.4byte	.LCFI162-.LFB239
	.byte	0xe
	.uleb128 0x28
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x87
	.uleb128 0x4
	.byte	0x88
	.uleb128 0x3
	.byte	0x89
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI163-.LCFI162
	.byte	0xa
	.byte	0xe
	.uleb128 0x1c
	.byte	0x4
	.4byte	.LCFI164-.LCFI163
	.byte	0xb
	.align	2
.LEFDE216:
.LSFDE218:
	.4byte	.LEFDE218-.LASFDE218
.LASFDE218:
	.4byte	.Lframe0
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.byte	0x4
	.4byte	.LCFI165-.LFB249
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE218:
.LSFDE220:
	.4byte	.LEFDE220-.LASFDE220
.LASFDE220:
	.4byte	.Lframe0
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.byte	0x4
	.4byte	.LCFI166-.LFB165
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI167-.LCFI166
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI168-.LCFI167
	.byte	0xe
	.uleb128 0x24
	.align	2
.LEFDE220:
.LSFDE222:
	.4byte	.LEFDE222-.LASFDE222
.LASFDE222:
	.4byte	.Lframe0
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.byte	0x4
	.4byte	.LCFI169-.LFB166
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE222:
.LSFDE224:
	.4byte	.LEFDE224-.LASFDE224
.LASFDE224:
	.4byte	.Lframe0
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.byte	0x4
	.4byte	.LCFI170-.LFB162
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE224:
.LSFDE226:
	.4byte	.LEFDE226-.LASFDE226
.LASFDE226:
	.4byte	.Lframe0
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.byte	0x4
	.4byte	.LCFI171-.LFB163
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE226:
.LSFDE228:
	.4byte	.LEFDE228-.LASFDE228
.LASFDE228:
	.4byte	.Lframe0
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.byte	0x4
	.4byte	.LCFI172-.LFB168
	.byte	0xe
	.uleb128 0x20
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x86
	.uleb128 0x4
	.byte	0x87
	.uleb128 0x3
	.byte	0x88
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI173-.LCFI172
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI174-.LCFI173
	.byte	0xb
	.align	2
.LEFDE228:
.LSFDE230:
	.4byte	.LEFDE230-.LASFDE230
.LASFDE230:
	.4byte	.Lframe0
	.4byte	.LFB258
	.4byte	.LFE258-.LFB258
	.byte	0x4
	.4byte	.LCFI175-.LFB258
	.byte	0xe
	.uleb128 0x30
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI176-.LCFI175
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI177-.LCFI176
	.byte	0xb
	.align	2
.LEFDE230:
.LSFDE232:
	.4byte	.LEFDE232-.LASFDE232
.LASFDE232:
	.4byte	.Lframe0
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.byte	0x4
	.4byte	.LCFI178-.LFB261
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE232:
.LSFDE234:
	.4byte	.LEFDE234-.LASFDE234
.LASFDE234:
	.4byte	.Lframe0
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.byte	0x4
	.4byte	.LCFI179-.LFB233
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE234:
.LSFDE236:
	.4byte	.LEFDE236-.LASFDE236
.LASFDE236:
	.4byte	.Lframe0
	.4byte	.LFB263
	.4byte	.LFE263-.LFB263
	.byte	0x4
	.4byte	.LCFI180-.LFB263
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE236:
.LSFDE238:
	.4byte	.LEFDE238-.LASFDE238
.LASFDE238:
	.4byte	.Lframe0
	.4byte	.LFB262
	.4byte	.LFE262-.LFB262
	.byte	0x4
	.4byte	.LCFI181-.LFB262
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI182-.LCFI181
	.byte	0xce
	.byte	0xc5
	.byte	0xc4
	.byte	0xc3
	.byte	0xe
	.uleb128 0
	.align	2
.LEFDE238:
.LSFDE240:
	.4byte	.LEFDE240-.LASFDE240
.LASFDE240:
	.4byte	.Lframe0
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.byte	0x4
	.4byte	.LCFI183-.LFB269
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI184-.LCFI183
	.byte	0xe
	.uleb128 0x48
	.byte	0x4
	.4byte	.LCFI185-.LCFI184
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI186-.LCFI185
	.byte	0xb
	.align	2
.LEFDE240:
.LSFDE242:
	.4byte	.LEFDE242-.LASFDE242
.LASFDE242:
	.4byte	.Lframe0
	.4byte	.LFB270
	.4byte	.LFE270-.LFB270
	.align	2
.LEFDE242:
.LSFDE244:
	.4byte	.LEFDE244-.LASFDE244
.LASFDE244:
	.4byte	.Lframe0
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.byte	0x4
	.4byte	.LCFI187-.LFB164
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI188-.LCFI187
	.byte	0xe
	.uleb128 0x60
	.byte	0x4
	.4byte	.LCFI189-.LCFI188
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI190-.LCFI189
	.byte	0xb
	.align	2
.LEFDE244:
.LSFDE246:
	.4byte	.LEFDE246-.LASFDE246
.LASFDE246:
	.4byte	.Lframe0
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.align	2
.LEFDE246:
.LSFDE248:
	.4byte	.LEFDE248-.LASFDE248
.LASFDE248:
	.4byte	.Lframe0
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.byte	0x4
	.4byte	.LCFI191-.LFB167
	.byte	0xe
	.uleb128 0x24
	.byte	0x84
	.uleb128 0x9
	.byte	0x85
	.uleb128 0x8
	.byte	0x86
	.uleb128 0x7
	.byte	0x87
	.uleb128 0x6
	.byte	0x88
	.uleb128 0x5
	.byte	0x89
	.uleb128 0x4
	.byte	0x8a
	.uleb128 0x3
	.byte	0x8b
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI192-.LCFI191
	.byte	0xe
	.uleb128 0x68
	.byte	0x4
	.4byte	.LCFI193-.LCFI192
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI194-.LCFI193
	.byte	0xb
	.align	2
.LEFDE248:
.LSFDE250:
	.4byte	.LEFDE250-.LASFDE250
.LASFDE250:
	.4byte	.Lframe0
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.align	2
.LEFDE250:
.LSFDE252:
	.4byte	.LEFDE252-.LASFDE252
.LASFDE252:
	.4byte	.Lframe0
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.byte	0x4
	.4byte	.LCFI195-.LFB224
	.byte	0xe
	.uleb128 0x18
	.byte	0x83
	.uleb128 0x6
	.byte	0x84
	.uleb128 0x5
	.byte	0x85
	.uleb128 0x4
	.byte	0x86
	.uleb128 0x3
	.byte	0x87
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE252:
.LSFDE254:
	.4byte	.LEFDE254-.LASFDE254
.LASFDE254:
	.4byte	.Lframe0
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.byte	0x4
	.4byte	.LCFI196-.LFB242
	.byte	0xe
	.uleb128 0x10
	.byte	0x84
	.uleb128 0x4
	.byte	0x85
	.uleb128 0x3
	.byte	0x86
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE254:
.LSFDE256:
	.4byte	.LEFDE256-.LASFDE256
.LASFDE256:
	.4byte	.Lframe0
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.byte	0x4
	.4byte	.LCFI197-.LFB160
	.byte	0xe
	.uleb128 0x20
	.byte	0x83
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x87
	.uleb128 0x4
	.byte	0x88
	.uleb128 0x3
	.byte	0x89
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE256:
.LSFDE258:
	.4byte	.LEFDE258-.LASFDE258
.LASFDE258:
	.4byte	.Lframe0
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.byte	0x4
	.4byte	.LCFI198-.LFB161
	.byte	0xe
	.uleb128 0x10
	.byte	0x83
	.uleb128 0x4
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE258:
.LSFDE260:
	.4byte	.LEFDE260-.LASFDE260
.LASFDE260:
	.4byte	.Lframe0
	.4byte	.LFB279
	.4byte	.LFE279-.LFB279
	.align	2
.LEFDE260:
	.text
.Letext0:
	.file 6 "include/asm-generic/int-ll64.h"
	.file 7 "./include/uapi/asm-generic/posix_types.h"
	.file 8 "include/linux/types.h"
	.file 9 "./arch/arm/include/asm/hwcap.h"
	.file 10 "include/linux/init.h"
	.file 11 "include/linux/printk.h"
	.file 12 "include/linux/kernel.h"
	.file 13 "drivers/rkflash/rksftl/sftl_inc.h"
	.file 14 "drivers/rkflash/rksftl/flash_com.h"
	.file 15 "drivers/rkflash/rksftl/sftl_struct.h"
	.file 16 "drivers/rkflash/rksftl/sftl_global.h"
	.file 17 "include/linux/string.h"
	.file 18 "./arch/arm/include/asm/string.h"
	.file 19 "drivers/rkflash/rksftl/typedef.h"
	.file 20 "<built-in>"
	.section	.debug_info,"",%progbits
.Ldebug_info0:
	.4byte	0x6763
	.2byte	0x4
	.4byte	.Ldebug_abbrev0
	.byte	0x4
	.uleb128 0x1
	.4byte	.LASF664
	.byte	0x1
	.4byte	.LASF665
	.4byte	.LASF666
	.4byte	.Ltext0
	.4byte	.Letext0-.Ltext0
	.4byte	.Ldebug_line0
	.uleb128 0x2
	.byte	0x4
	.uleb128 0x3
	.byte	0x4
	.byte	0x7
	.4byte	.LASF0
	.uleb128 0x3
	.byte	0x4
	.byte	0x7
	.4byte	.LASF1
	.uleb128 0x3
	.byte	0x1
	.byte	0x8
	.4byte	.LASF2
	.uleb128 0x4
	.4byte	0x35
	.uleb128 0x3
	.byte	0x4
	.byte	0x7
	.4byte	.LASF3
	.uleb128 0x3
	.byte	0x1
	.byte	0x6
	.4byte	.LASF4
	.uleb128 0x3
	.byte	0x1
	.byte	0x8
	.4byte	.LASF5
	.uleb128 0x3
	.byte	0x2
	.byte	0x5
	.4byte	.LASF6
	.uleb128 0x3
	.byte	0x2
	.byte	0x7
	.4byte	.LASF7
	.uleb128 0x5
	.byte	0x4
	.byte	0x5
	.ascii	"int\000"
	.uleb128 0x3
	.byte	0x8
	.byte	0x5
	.4byte	.LASF8
	.uleb128 0x3
	.byte	0x8
	.byte	0x7
	.4byte	.LASF9
	.uleb128 0x6
	.ascii	"u8\000"
	.byte	0x6
	.byte	0x10
	.4byte	0x4f
	.uleb128 0x6
	.ascii	"u16\000"
	.byte	0x6
	.byte	0x13
	.4byte	0x5d
	.uleb128 0x6
	.ascii	"s32\000"
	.byte	0x6
	.byte	0x15
	.4byte	0x64
	.uleb128 0x6
	.ascii	"u32\000"
	.byte	0x6
	.byte	0x16
	.4byte	0x41
	.uleb128 0x3
	.byte	0x4
	.byte	0x5
	.4byte	.LASF10
	.uleb128 0x7
	.4byte	.LASF11
	.byte	0x7
	.byte	0x43
	.4byte	0x41
	.uleb128 0x8
	.byte	0x4
	.4byte	0x35
	.uleb128 0x7
	.4byte	.LASF12
	.byte	0x8
	.byte	0x1d
	.4byte	0xc7
	.uleb128 0x3
	.byte	0x1
	.byte	0x2
	.4byte	.LASF13
	.uleb128 0x7
	.4byte	.LASF14
	.byte	0x8
	.byte	0x36
	.4byte	0xab
	.uleb128 0x9
	.4byte	.LASF15
	.byte	0x9
	.byte	0xd
	.4byte	0x41
	.uleb128 0x9
	.4byte	.LASF16
	.byte	0x9
	.byte	0xd
	.4byte	0x41
	.uleb128 0x7
	.4byte	.LASF17
	.byte	0xa
	.byte	0x83
	.4byte	0xfa
	.uleb128 0x8
	.byte	0x4
	.4byte	0x100
	.uleb128 0xa
	.4byte	0x64
	.uleb128 0x8
	.byte	0x4
	.4byte	0x10b
	.uleb128 0xb
	.uleb128 0xc
	.4byte	0xef
	.4byte	0x117
	.uleb128 0xd
	.byte	0
	.uleb128 0x9
	.4byte	.LASF18
	.byte	0xa
	.byte	0x86
	.4byte	0x10c
	.uleb128 0x9
	.4byte	.LASF19
	.byte	0xa
	.byte	0x86
	.4byte	0x10c
	.uleb128 0x9
	.4byte	.LASF20
	.byte	0xa
	.byte	0x87
	.4byte	0x10c
	.uleb128 0x9
	.4byte	.LASF21
	.byte	0xa
	.byte	0x87
	.4byte	0x10c
	.uleb128 0xc
	.4byte	0x35
	.4byte	0x14e
	.uleb128 0xd
	.byte	0
	.uleb128 0x9
	.4byte	.LASF22
	.byte	0xa
	.byte	0x8e
	.4byte	0x143
	.uleb128 0x9
	.4byte	.LASF23
	.byte	0xa
	.byte	0x8f
	.4byte	0xb6
	.uleb128 0x9
	.4byte	.LASF24
	.byte	0xa
	.byte	0x90
	.4byte	0x41
	.uleb128 0x9
	.4byte	.LASF25
	.byte	0xa
	.byte	0x9c
	.4byte	0x105
	.uleb128 0x9
	.4byte	.LASF26
	.byte	0xa
	.byte	0x9e
	.4byte	0xbc
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x190
	.uleb128 0xd
	.byte	0
	.uleb128 0x4
	.4byte	0x185
	.uleb128 0x9
	.4byte	.LASF27
	.byte	0xb
	.byte	0xa
	.4byte	0x190
	.uleb128 0x9
	.4byte	.LASF28
	.byte	0xb
	.byte	0xb
	.4byte	0x190
	.uleb128 0xc
	.4byte	0x64
	.4byte	0x1b6
	.uleb128 0xd
	.byte	0
	.uleb128 0x9
	.4byte	.LASF29
	.byte	0xb
	.byte	0x2e
	.4byte	0x1ab
	.uleb128 0x9
	.4byte	.LASF30
	.byte	0xb
	.byte	0x9f
	.4byte	0x64
	.uleb128 0x9
	.4byte	.LASF31
	.byte	0xb
	.byte	0xa0
	.4byte	0x64
	.uleb128 0x9
	.4byte	.LASF32
	.byte	0xb
	.byte	0xa1
	.4byte	0x64
	.uleb128 0xe
	.4byte	.LASF34
	.uleb128 0x4
	.4byte	0x1e2
	.uleb128 0xf
	.4byte	.LASF33
	.byte	0xb
	.2byte	0x1a4
	.4byte	0x1e7
	.uleb128 0xe
	.4byte	.LASF35
	.uleb128 0xf
	.4byte	.LASF36
	.byte	0xc
	.2byte	0x104
	.4byte	0x1f8
	.uleb128 0x10
	.4byte	0xa4
	.4byte	0x218
	.uleb128 0x11
	.4byte	0x64
	.byte	0
	.uleb128 0xf
	.4byte	.LASF37
	.byte	0xc
	.2byte	0x105
	.4byte	0x224
	.uleb128 0x8
	.byte	0x4
	.4byte	0x209
	.uleb128 0xf
	.4byte	.LASF38
	.byte	0xc
	.2byte	0x1bf
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF39
	.byte	0xc
	.2byte	0x1c0
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF40
	.byte	0xc
	.2byte	0x1c1
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF41
	.byte	0xc
	.2byte	0x1c2
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF42
	.byte	0xc
	.2byte	0x1c3
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF43
	.byte	0xc
	.2byte	0x1c4
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF44
	.byte	0xc
	.2byte	0x1c5
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF45
	.byte	0xc
	.2byte	0x1c7
	.4byte	0xbc
	.uleb128 0xf
	.4byte	.LASF46
	.byte	0xc
	.2byte	0x1da
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF47
	.byte	0xc
	.2byte	0x1dc
	.4byte	0xbc
	.uleb128 0x12
	.4byte	.LASF667
	.byte	0x4
	.4byte	0x41
	.byte	0xc
	.2byte	0x1df
	.4byte	0x2d2
	.uleb128 0x13
	.4byte	.LASF48
	.byte	0
	.uleb128 0x13
	.4byte	.LASF49
	.byte	0x1
	.uleb128 0x13
	.4byte	.LASF50
	.byte	0x2
	.uleb128 0x13
	.4byte	.LASF51
	.byte	0x3
	.uleb128 0x13
	.4byte	.LASF52
	.byte	0x4
	.byte	0
	.uleb128 0xf
	.4byte	.LASF53
	.byte	0xc
	.2byte	0x1e5
	.4byte	0x2a2
	.uleb128 0xf
	.4byte	.LASF54
	.byte	0xc
	.2byte	0x1f8
	.4byte	0x190
	.uleb128 0xf
	.4byte	.LASF55
	.byte	0xc
	.2byte	0x203
	.4byte	0x190
	.uleb128 0x7
	.4byte	.LASF56
	.byte	0xd
	.byte	0x9
	.4byte	0x4f
	.uleb128 0x7
	.4byte	.LASF57
	.byte	0xd
	.byte	0xa
	.4byte	0x5d
	.uleb128 0x7
	.4byte	.LASF58
	.byte	0xd
	.byte	0xb
	.4byte	0x41
	.uleb128 0x7
	.4byte	.LASF59
	.byte	0xd
	.byte	0xc
	.4byte	0x64
	.uleb128 0x7
	.4byte	.LASF60
	.byte	0xd
	.byte	0xd
	.4byte	0x56
	.uleb128 0x14
	.4byte	.LASF65
	.byte	0x14
	.byte	0xe
	.byte	0x22
	.4byte	0x376
	.uleb128 0x15
	.4byte	.LASF61
	.byte	0xe
	.byte	0x23
	.4byte	0x99
	.byte	0
	.uleb128 0x15
	.4byte	.LASF62
	.byte	0xe
	.byte	0x24
	.4byte	0x99
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF63
	.byte	0xe
	.byte	0x25
	.4byte	0x376
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF64
	.byte	0xe
	.byte	0x26
	.4byte	0x376
	.byte	0xc
	.uleb128 0x16
	.ascii	"lpa\000"
	.byte	0xe
	.byte	0x27
	.4byte	0x99
	.byte	0x10
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x99
	.uleb128 0x14
	.4byte	.LASF66
	.byte	0x18
	.byte	0xe
	.byte	0x2a
	.4byte	0x419
	.uleb128 0x15
	.4byte	.LASF67
	.byte	0xe
	.byte	0x2b
	.4byte	0x83
	.byte	0
	.uleb128 0x15
	.4byte	.LASF68
	.byte	0xe
	.byte	0x2c
	.4byte	0x83
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF69
	.byte	0xe
	.byte	0x2d
	.4byte	0x83
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF70
	.byte	0xe
	.byte	0x2e
	.4byte	0x83
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF71
	.byte	0xe
	.byte	0x2f
	.4byte	0x83
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF72
	.byte	0xe
	.byte	0x30
	.4byte	0x83
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF73
	.byte	0xe
	.byte	0x31
	.4byte	0x83
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF74
	.byte	0xe
	.byte	0x32
	.4byte	0x83
	.byte	0xe
	.uleb128 0x15
	.4byte	.LASF75
	.byte	0xe
	.byte	0x33
	.4byte	0x83
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF76
	.byte	0xe
	.byte	0x34
	.4byte	0x83
	.byte	0x12
	.uleb128 0x15
	.4byte	.LASF77
	.byte	0xe
	.byte	0x35
	.4byte	0x83
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF78
	.byte	0xe
	.byte	0x36
	.4byte	0x79
	.byte	0x16
	.byte	0
	.uleb128 0x4
	.4byte	0x37c
	.uleb128 0x14
	.4byte	.LASF79
	.byte	0x10
	.byte	0xe
	.byte	0x39
	.4byte	0x45b
	.uleb128 0x15
	.4byte	.LASF80
	.byte	0xe
	.byte	0x3a
	.4byte	0x475
	.byte	0
	.uleb128 0x15
	.4byte	.LASF81
	.byte	0xe
	.byte	0x3b
	.4byte	0x48f
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF82
	.byte	0xe
	.byte	0x3c
	.4byte	0x4b3
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF83
	.byte	0xe
	.byte	0x3d
	.4byte	0x4b3
	.byte	0xc
	.byte	0
	.uleb128 0x10
	.4byte	0x8e
	.4byte	0x46f
	.uleb128 0x11
	.4byte	0x46f
	.uleb128 0x11
	.4byte	0x99
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x83
	.uleb128 0x8
	.byte	0x4
	.4byte	0x45b
	.uleb128 0x10
	.4byte	0x99
	.4byte	0x48f
	.uleb128 0x11
	.4byte	0x79
	.uleb128 0x11
	.4byte	0x99
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x47b
	.uleb128 0x10
	.4byte	0x99
	.4byte	0x4b3
	.uleb128 0x11
	.4byte	0x79
	.uleb128 0x11
	.4byte	0x99
	.uleb128 0x11
	.4byte	0x376
	.uleb128 0x11
	.4byte	0x376
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x495
	.uleb128 0x14
	.4byte	.LASF84
	.byte	0x3c
	.byte	0xf
	.byte	0x2b
	.4byte	0x519
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0x2d
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF85
	.byte	0xf
	.byte	0x2e
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0xf
	.byte	0x2f
	.4byte	0x301
	.byte	0x4
	.uleb128 0x16
	.ascii	"cnt\000"
	.byte	0xf
	.byte	0x30
	.4byte	0x301
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x31
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0xf
	.byte	0x32
	.4byte	0x519
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF89
	.byte	0xf
	.byte	0x33
	.4byte	0x529
	.byte	0x1c
	.byte	0
	.uleb128 0xc
	.4byte	0x301
	.4byte	0x529
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x7
	.byte	0
	.uleb128 0xc
	.4byte	0x539
	.4byte	0x539
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x7
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x30c
	.uleb128 0x14
	.4byte	.LASF90
	.byte	0xc
	.byte	0xf
	.byte	0x3f
	.4byte	0x587
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0x41
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF85
	.byte	0xf
	.byte	0x42
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0xf
	.byte	0x43
	.4byte	0x301
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xf
	.byte	0x44
	.4byte	0x301
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x45
	.4byte	0x30c
	.byte	0x8
	.byte	0
	.uleb128 0x14
	.4byte	.LASF92
	.byte	0x28
	.byte	0xf
	.byte	0x48
	.4byte	0x630
	.uleb128 0x15
	.4byte	.LASF93
	.byte	0xf
	.byte	0x4a
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF85
	.byte	0xf
	.byte	0x4b
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xf
	.byte	0x4d
	.4byte	0x301
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0xf
	.byte	0x4e
	.4byte	0x301
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF96
	.byte	0xf
	.byte	0x50
	.4byte	0x301
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF97
	.byte	0xf
	.byte	0x51
	.4byte	0x301
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF98
	.byte	0xf
	.byte	0x53
	.4byte	0x630
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF99
	.byte	0xf
	.byte	0x54
	.4byte	0x539
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF100
	.byte	0xf
	.byte	0x55
	.4byte	0x630
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF101
	.byte	0xf
	.byte	0x56
	.4byte	0x539
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x58
	.4byte	0x30c
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF102
	.byte	0xf
	.byte	0x59
	.4byte	0x30c
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF103
	.byte	0xf
	.byte	0x5a
	.4byte	0x30c
	.byte	0x24
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x301
	.uleb128 0x14
	.4byte	.LASF104
	.byte	0xc
	.byte	0xf
	.byte	0x5d
	.4byte	0x673
	.uleb128 0x15
	.4byte	.LASF105
	.byte	0xf
	.byte	0x5f
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF106
	.byte	0xf
	.byte	0x60
	.4byte	0x301
	.byte	0x2
	.uleb128 0x16
	.ascii	"hit\000"
	.byte	0xf
	.byte	0x61
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF107
	.byte	0xf
	.byte	0x62
	.4byte	0x539
	.byte	0x8
	.byte	0
	.uleb128 0x14
	.4byte	.LASF108
	.byte	0x24
	.byte	0xf
	.byte	0x65
	.4byte	0x6e0
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xf
	.byte	0x67
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF109
	.byte	0xf
	.byte	0x68
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x69
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x16
	.ascii	"len\000"
	.byte	0xf
	.byte	0x6a
	.4byte	0x30c
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF110
	.byte	0xf
	.byte	0x6b
	.4byte	0x30c
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF111
	.byte	0xf
	.byte	0x6c
	.4byte	0x30c
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0xf
	.byte	0x6d
	.4byte	0x6e0
	.byte	0x18
	.uleb128 0x16
	.ascii	"tbl\000"
	.byte	0xf
	.byte	0x6e
	.4byte	0x6f0
	.byte	0x20
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x6f0
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1
	.byte	0
	.uleb128 0xc
	.4byte	0x301
	.4byte	0x700
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1
	.byte	0
	.uleb128 0x14
	.4byte	.LASF113
	.byte	0x10
	.byte	0xf
	.byte	0x72
	.4byte	0x760
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xf
	.byte	0x74
	.4byte	0x301
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0x75
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x76
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0xf
	.byte	0x77
	.4byte	0x301
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF114
	.byte	0xf
	.byte	0x78
	.4byte	0x301
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF115
	.byte	0xf
	.byte	0x79
	.4byte	0x301
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0xf
	.byte	0x7a
	.4byte	0x760
	.byte	0xe
	.byte	0
	.uleb128 0xc
	.4byte	0x2f6
	.4byte	0x770
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1
	.byte	0
	.uleb128 0x14
	.4byte	.LASF116
	.byte	0x10
	.byte	0xf
	.byte	0x7d
	.4byte	0x7b8
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xf
	.byte	0x7f
	.4byte	0x301
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0x80
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x81
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x16
	.ascii	"lpa\000"
	.byte	0xf
	.byte	0x82
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF117
	.byte	0xf
	.byte	0x83
	.4byte	0x30c
	.byte	0xc
	.byte	0
	.uleb128 0x14
	.4byte	.LASF118
	.byte	0x10
	.byte	0xf
	.byte	0x8f
	.4byte	0x800
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xf
	.byte	0x91
	.4byte	0x301
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0x92
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x93
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF119
	.byte	0xf
	.byte	0x94
	.4byte	0x301
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0xf
	.byte	0x95
	.4byte	0x800
	.byte	0xa
	.byte	0
	.uleb128 0xc
	.4byte	0x2f6
	.4byte	0x810
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x5
	.byte	0
	.uleb128 0x14
	.4byte	.LASF120
	.byte	0x10
	.byte	0xf
	.byte	0x98
	.4byte	0x858
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0xf
	.byte	0x9a
	.4byte	0x301
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0x9b
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0xf
	.byte	0x9c
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF121
	.byte	0xf
	.byte	0x9d
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF122
	.byte	0xf
	.byte	0x9e
	.4byte	0x30c
	.byte	0xc
	.byte	0
	.uleb128 0x14
	.4byte	.LASF123
	.byte	0x30
	.byte	0xf
	.byte	0xa1
	.4byte	0x955
	.uleb128 0x16
	.ascii	"tag\000"
	.byte	0xf
	.byte	0xa3
	.4byte	0x30c
	.byte	0
	.uleb128 0x16
	.ascii	"ver\000"
	.byte	0xf
	.byte	0xa4
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF91
	.byte	0xf
	.byte	0xa6
	.4byte	0x301
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF124
	.byte	0xf
	.byte	0xa7
	.4byte	0x2f6
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF125
	.byte	0xf
	.byte	0xa8
	.4byte	0x2f6
	.byte	0xb
	.uleb128 0x15
	.4byte	.LASF126
	.byte	0xf
	.byte	0xa9
	.4byte	0x2f6
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF127
	.byte	0xf
	.byte	0xaa
	.4byte	0x2f6
	.byte	0xd
	.uleb128 0x15
	.4byte	.LASF128
	.byte	0xf
	.byte	0xab
	.4byte	0x301
	.byte	0xe
	.uleb128 0x15
	.4byte	.LASF129
	.byte	0xf
	.byte	0xad
	.4byte	0x301
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF130
	.byte	0xf
	.byte	0xae
	.4byte	0x301
	.byte	0x12
	.uleb128 0x15
	.4byte	.LASF131
	.byte	0xf
	.byte	0xaf
	.4byte	0x301
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF132
	.byte	0xf
	.byte	0xb0
	.4byte	0x301
	.byte	0x16
	.uleb128 0x15
	.4byte	.LASF133
	.byte	0xf
	.byte	0xb2
	.4byte	0x301
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF134
	.byte	0xf
	.byte	0xb3
	.4byte	0x301
	.byte	0x1a
	.uleb128 0x15
	.4byte	.LASF135
	.byte	0xf
	.byte	0xb4
	.4byte	0x301
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF136
	.byte	0xf
	.byte	0xb5
	.4byte	0x301
	.byte	0x1e
	.uleb128 0x15
	.4byte	.LASF137
	.byte	0xf
	.byte	0xb7
	.4byte	0x30c
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF138
	.byte	0xf
	.byte	0xb8
	.4byte	0x30c
	.byte	0x24
	.uleb128 0x15
	.4byte	.LASF139
	.byte	0xf
	.byte	0xb9
	.4byte	0x30c
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0xf
	.byte	0xba
	.4byte	0x6f0
	.byte	0x2c
	.byte	0
	.uleb128 0x18
	.4byte	.LASF140
	.2byte	0x200
	.byte	0xf
	.byte	0xbe
	.4byte	0xa9b
	.uleb128 0x16
	.ascii	"tag\000"
	.byte	0xf
	.byte	0xc0
	.4byte	0x30c
	.byte	0
	.uleb128 0x16
	.ascii	"ver\000"
	.byte	0xf
	.byte	0xc1
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF141
	.byte	0xf
	.byte	0xc3
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF142
	.byte	0xf
	.byte	0xc4
	.4byte	0x30c
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF143
	.byte	0xf
	.byte	0xc5
	.4byte	0x30c
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF144
	.byte	0xf
	.byte	0xc6
	.4byte	0x30c
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF137
	.byte	0xf
	.byte	0xc8
	.4byte	0x30c
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF145
	.byte	0xf
	.byte	0xc9
	.4byte	0x30c
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF146
	.byte	0xf
	.byte	0xca
	.4byte	0x30c
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF147
	.byte	0xf
	.byte	0xcb
	.4byte	0x30c
	.byte	0x24
	.uleb128 0x15
	.4byte	.LASF134
	.byte	0xf
	.byte	0xcd
	.4byte	0x30c
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF148
	.byte	0xf
	.byte	0xce
	.4byte	0x30c
	.byte	0x2c
	.uleb128 0x15
	.4byte	.LASF149
	.byte	0xf
	.byte	0xcf
	.4byte	0x30c
	.byte	0x30
	.uleb128 0x15
	.4byte	.LASF150
	.byte	0xf
	.byte	0xd0
	.4byte	0x30c
	.byte	0x34
	.uleb128 0x15
	.4byte	.LASF151
	.byte	0xf
	.byte	0xd2
	.4byte	0x30c
	.byte	0x38
	.uleb128 0x15
	.4byte	.LASF152
	.byte	0xf
	.byte	0xd3
	.4byte	0x30c
	.byte	0x3c
	.uleb128 0x15
	.4byte	.LASF153
	.byte	0xf
	.byte	0xd4
	.4byte	0x30c
	.byte	0x40
	.uleb128 0x15
	.4byte	.LASF154
	.byte	0xf
	.byte	0xd5
	.4byte	0x30c
	.byte	0x44
	.uleb128 0x15
	.4byte	.LASF155
	.byte	0xf
	.byte	0xd7
	.4byte	0x30c
	.byte	0x48
	.uleb128 0x15
	.4byte	.LASF156
	.byte	0xf
	.byte	0xd8
	.4byte	0x30c
	.byte	0x4c
	.uleb128 0x15
	.4byte	.LASF157
	.byte	0xf
	.byte	0xd9
	.4byte	0x30c
	.byte	0x50
	.uleb128 0x15
	.4byte	.LASF158
	.byte	0xf
	.byte	0xda
	.4byte	0x30c
	.byte	0x54
	.uleb128 0x15
	.4byte	.LASF159
	.byte	0xf
	.byte	0xdc
	.4byte	0x30c
	.byte	0x58
	.uleb128 0x15
	.4byte	.LASF160
	.byte	0xf
	.byte	0xdd
	.4byte	0x30c
	.byte	0x5c
	.uleb128 0x15
	.4byte	.LASF161
	.byte	0xf
	.byte	0xde
	.4byte	0x30c
	.byte	0x60
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0xf
	.byte	0xe0
	.4byte	0xa9b
	.byte	0x64
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0xaab
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x66
	.byte	0
	.uleb128 0x14
	.4byte	.LASF162
	.byte	0x6
	.byte	0xf
	.byte	0xe4
	.4byte	0xadc
	.uleb128 0x15
	.4byte	.LASF163
	.byte	0xf
	.byte	0xe6
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF164
	.byte	0xf
	.byte	0xe7
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF165
	.byte	0xf
	.byte	0xe8
	.4byte	0x301
	.byte	0x4
	.byte	0
	.uleb128 0x18
	.4byte	.LASF166
	.2byte	0x808
	.byte	0xf
	.byte	0xeb
	.4byte	0xb26
	.uleb128 0x16
	.ascii	"max\000"
	.byte	0xf
	.byte	0xed
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF167
	.byte	0xf
	.byte	0xee
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF168
	.byte	0xf
	.byte	0xef
	.4byte	0x301
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF114
	.byte	0xf
	.byte	0xf0
	.4byte	0x301
	.byte	0x6
	.uleb128 0x16
	.ascii	"arr\000"
	.byte	0xf
	.byte	0xf1
	.4byte	0xb26
	.byte	0x8
	.byte	0
	.uleb128 0xc
	.4byte	0x301
	.4byte	0xb37
	.uleb128 0x19
	.4byte	0x2e
	.2byte	0x3ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF169
	.byte	0x30
	.byte	0xf
	.byte	0xf5
	.4byte	0xbc9
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0xf
	.byte	0xf7
	.4byte	0x301
	.byte	0
	.uleb128 0x15
	.4byte	.LASF170
	.byte	0xf
	.byte	0xf8
	.4byte	0x301
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF171
	.byte	0xf
	.byte	0xf9
	.4byte	0x301
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF172
	.byte	0xf
	.byte	0xfa
	.4byte	0x2f6
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF173
	.byte	0xf
	.byte	0xfb
	.4byte	0x2f6
	.byte	0x7
	.uleb128 0x15
	.4byte	.LASF174
	.byte	0xf
	.byte	0xfc
	.4byte	0x2f6
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF175
	.byte	0xf
	.byte	0xfd
	.4byte	0x2f6
	.byte	0x9
	.uleb128 0x15
	.4byte	.LASF176
	.byte	0xf
	.byte	0xfe
	.4byte	0x2f6
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF177
	.byte	0xf
	.byte	0xff
	.4byte	0x2f6
	.byte	0xb
	.uleb128 0x1a
	.4byte	.LASF87
	.byte	0xf
	.2byte	0x100
	.4byte	0x30c
	.byte	0xc
	.uleb128 0x1a
	.4byte	.LASF178
	.byte	0xf
	.2byte	0x101
	.4byte	0xbc9
	.byte	0x10
	.byte	0
	.uleb128 0xc
	.4byte	0x301
	.4byte	0xbd9
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xf
	.byte	0
	.uleb128 0x1b
	.4byte	.LASF179
	.byte	0xc
	.byte	0xf
	.2byte	0x105
	.4byte	0xc0e
	.uleb128 0x1a
	.4byte	.LASF180
	.byte	0xf
	.2byte	0x107
	.4byte	0x30c
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF181
	.byte	0xf
	.2byte	0x108
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x1c
	.ascii	"lpa\000"
	.byte	0xf
	.2byte	0x109
	.4byte	0x30c
	.byte	0x8
	.byte	0
	.uleb128 0x1b
	.4byte	.LASF182
	.byte	0xc
	.byte	0xf
	.2byte	0x10c
	.4byte	0xc43
	.uleb128 0x1a
	.4byte	.LASF63
	.byte	0xf
	.2byte	0x10e
	.4byte	0x539
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF64
	.byte	0xf
	.2byte	0x10f
	.4byte	0x539
	.byte	0x4
	.uleb128 0x1a
	.4byte	.LASF183
	.byte	0xf
	.2byte	0x110
	.4byte	0x30c
	.byte	0x8
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF184
	.byte	0x10
	.byte	0x12
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_type
	.uleb128 0x1d
	.4byte	.LASF185
	.byte	0x10
	.byte	0x13
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_die_num
	.uleb128 0x1d
	.4byte	.LASF186
	.byte	0x10
	.byte	0x14
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_planes_per_die
	.uleb128 0x1d
	.4byte	.LASF187
	.byte	0x10
	.byte	0x15
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blks_per_die
	.uleb128 0x1d
	.4byte	.LASF188
	.byte	0x10
	.byte	0x16
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blks_per_die_shift
	.uleb128 0x1d
	.4byte	.LASF189
	.byte	0x10
	.byte	0x17
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_planes_num
	.uleb128 0x1d
	.4byte	.LASF190
	.byte	0x10
	.byte	0x18
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blk_pre_plane
	.uleb128 0x1d
	.4byte	.LASF191
	.byte	0x10
	.byte	0x19
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_ext_blk_pre_plane
	.uleb128 0x1d
	.4byte	.LASF192
	.byte	0x10
	.byte	0x1a
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_bbm_buf_size
	.uleb128 0x1d
	.4byte	.LASF193
	.byte	0x10
	.byte	0x1c
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_blk
	.uleb128 0x1d
	.4byte	.LASF194
	.byte	0x10
	.byte	0x1d
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_slc_blk
	.uleb128 0x1d
	.4byte	.LASF195
	.byte	0x10
	.byte	0x1e
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_super_blk
	.uleb128 0x1d
	.4byte	.LASF196
	.byte	0x10
	.byte	0x1f
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sec_pre_page
	.uleb128 0x1d
	.4byte	.LASF197
	.byte	0x10
	.byte	0x20
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sec_pre_page_shift
	.uleb128 0x1d
	.4byte	.LASF198
	.byte	0x10
	.byte	0x24
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_byte_pre_page
	.uleb128 0x1d
	.4byte	.LASF199
	.byte	0x10
	.byte	0x25
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_byte_pre_oob
	.uleb128 0x1d
	.4byte	.LASF200
	.byte	0x10
	.byte	0x26
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_reserved_blks
	.uleb128 0x1d
	.4byte	.LASF201
	.byte	0x10
	.byte	0x27
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_totle_phy_blks
	.uleb128 0x1d
	.4byte	.LASF202
	.byte	0x10
	.byte	0x29
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_map_region_num
	.uleb128 0x1d
	.4byte	.LASF203
	.byte	0x10
	.byte	0x2a
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_l2pmap_ram_region_num
	.uleb128 0x1d
	.4byte	.LASF204
	.byte	0x10
	.byte	0x2c
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_vendor_region_num
	.uleb128 0x1d
	.4byte	.LASF205
	.byte	0x10
	.byte	0x2e
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_map_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF206
	.byte	0x10
	.byte	0x2f
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_map_blks
	.uleb128 0x1d
	.4byte	.LASF207
	.byte	0x10
	.byte	0x30
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_vendor_blks
	.uleb128 0x1d
	.4byte	.LASF208
	.byte	0x10
	.byte	0x31
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_vendor_part_size
	.uleb128 0x1d
	.4byte	.LASF209
	.byte	0x10
	.byte	0x32
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sys_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF210
	.byte	0x10
	.byte	0x33
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_init_sys_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF211
	.byte	0x10
	.byte	0x34
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_sys_blks
	.uleb128 0x1d
	.4byte	.LASF212
	.byte	0x10
	.byte	0x35
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_data_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF213
	.byte	0x10
	.byte	0x36
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_data_op_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF214
	.byte	0x10
	.byte	0x37
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_data_blks
	.uleb128 0x1d
	.4byte	.LASF215
	.byte	0x10
	.byte	0x38
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_gc_temp_power_lost_recovery_flag
	.uleb128 0x1d
	.4byte	.LASF216
	.byte	0x10
	.byte	0x3a
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_GlobalSysVersion
	.uleb128 0x1d
	.4byte	.LASF217
	.byte	0x10
	.byte	0x3b
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_GlobalDataVersion
	.uleb128 0x1d
	.4byte	.LASF218
	.byte	0x10
	.byte	0x3c
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLbaSector
	.uleb128 0x1d
	.4byte	.LASF219
	.byte	0x10
	.byte	0x3d
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLpn
	.uleb128 0x1d
	.4byte	.LASF220
	.byte	0x10
	.byte	0x3e
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_VaildLpn
	.uleb128 0x1d
	.4byte	.LASF221
	.byte	0x10
	.byte	0x3f
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLbn
	.uleb128 0x1d
	.4byte	.LASF222
	.byte	0x10
	.byte	0x41
	.4byte	0x4b9
	.uleb128 0x5
	.byte	0x3
	.4byte	gBbtInfo
	.uleb128 0x1d
	.4byte	.LASF223
	.byte	0x10
	.byte	0x42
	.4byte	0x53f
	.uleb128 0x5
	.byte	0x3
	.4byte	gSysInfo
	.uleb128 0x1d
	.4byte	.LASF224
	.byte	0x10
	.byte	0x43
	.4byte	0xadc
	.uleb128 0x5
	.byte	0x3
	.4byte	gSysFreeQueue
	.uleb128 0x1d
	.4byte	.LASF225
	.byte	0x10
	.byte	0x44
	.4byte	0x587
	.uleb128 0x5
	.byte	0x3
	.4byte	gL2pMapInfo
	.uleb128 0x1d
	.4byte	.LASF226
	.byte	0x10
	.byte	0x45
	.4byte	0x587
	.uleb128 0x5
	.byte	0x3
	.4byte	gVendorBlkInfo
	.uleb128 0x1d
	.4byte	.LASF227
	.byte	0x10
	.byte	0x46
	.4byte	0x32d
	.uleb128 0x5
	.byte	0x3
	.4byte	req_sys
	.uleb128 0x1d
	.4byte	.LASF228
	.byte	0x10
	.byte	0x47
	.4byte	0xf40
	.uleb128 0x5
	.byte	0x3
	.4byte	req_read
	.uleb128 0x8
	.byte	0x4
	.4byte	0x32d
	.uleb128 0x1d
	.4byte	.LASF229
	.byte	0x10
	.byte	0x48
	.4byte	0xf40
	.uleb128 0x5
	.byte	0x3
	.4byte	req_prgm
	.uleb128 0x1d
	.4byte	.LASF230
	.byte	0x10
	.byte	0x49
	.4byte	0xf40
	.uleb128 0x5
	.byte	0x3
	.4byte	req_erase
	.uleb128 0x1d
	.4byte	.LASF231
	.byte	0x10
	.byte	0x4a
	.4byte	0xf40
	.uleb128 0x5
	.byte	0x3
	.4byte	req_gc
	.uleb128 0x1d
	.4byte	.LASF232
	.byte	0x10
	.byte	0x4b
	.4byte	0xf40
	.uleb128 0x5
	.byte	0x3
	.4byte	req_gc_dst
	.uleb128 0x1d
	.4byte	.LASF233
	.byte	0x10
	.byte	0x4d
	.4byte	0xf40
	.uleb128 0x5
	.byte	0x3
	.4byte	g_req_cache
	.uleb128 0xc
	.4byte	0x2f6
	.4byte	0xfab
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1f
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF234
	.byte	0x10
	.byte	0x4f
	.4byte	0xf9b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_plane_order_table
	.uleb128 0x1d
	.4byte	.LASF235
	.byte	0x10
	.byte	0x51
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_data_buf
	.uleb128 0x1d
	.4byte	.LASF236
	.byte	0x10
	.byte	0x52
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_data_buf_1
	.uleb128 0x1d
	.4byte	.LASF237
	.byte	0x10
	.byte	0x53
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_data_buf
	.uleb128 0x1d
	.4byte	.LASF238
	.byte	0x10
	.byte	0x54
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_spare_buf
	.uleb128 0x1d
	.4byte	.LASF239
	.byte	0x10
	.byte	0x55
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_data_buf_0
	.uleb128 0x1d
	.4byte	.LASF240
	.byte	0x10
	.byte	0x56
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_data_buf_1
	.uleb128 0x1d
	.4byte	.LASF241
	.byte	0x10
	.byte	0x57
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_spare_buf
	.uleb128 0x1d
	.4byte	.LASF242
	.byte	0x10
	.byte	0x58
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_spare_buf
	.uleb128 0x1d
	.4byte	.LASF243
	.byte	0x10
	.byte	0x59
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_data_buf
	.uleb128 0x1d
	.4byte	.LASF244
	.byte	0x10
	.byte	0x5a
	.4byte	0x1066
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_gc_page_buf_info
	.uleb128 0x8
	.byte	0x4
	.4byte	0xc0e
	.uleb128 0x1d
	.4byte	.LASF245
	.byte	0x10
	.byte	0x5b
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_gc_page_buf_num
	.uleb128 0x1d
	.4byte	.LASF246
	.byte	0x10
	.byte	0x5c
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_num_req
	.uleb128 0x1d
	.4byte	.LASF247
	.byte	0x10
	.byte	0x5d
	.4byte	0x109f
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_ect_tbl_info
	.uleb128 0x8
	.byte	0x4
	.4byte	0x673
	.uleb128 0x1d
	.4byte	.LASF248
	.byte	0x10
	.byte	0x5e
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_info_size
	.uleb128 0x1d
	.4byte	.LASF249
	.byte	0x10
	.byte	0x5f
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_erase_count_table
	.uleb128 0x1d
	.4byte	.LASF250
	.byte	0x10
	.byte	0x61
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_swl_mul_table
	.uleb128 0x1d
	.4byte	.LASF251
	.byte	0x10
	.byte	0x62
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_swl_count
	.uleb128 0x1d
	.4byte	.LASF252
	.byte	0x10
	.byte	0x63
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_valid_page_count_table
	.uleb128 0x1d
	.4byte	.LASF253
	.byte	0x10
	.byte	0x64
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_valid_page_count_check_table
	.uleb128 0x1d
	.4byte	.LASF254
	.byte	0x10
	.byte	0x65
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_blk_mode_table
	.uleb128 0x1d
	.4byte	.LASF255
	.byte	0x10
	.byte	0x67
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_table
	.uleb128 0x1d
	.4byte	.LASF256
	.byte	0x10
	.byte	0x68
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_valid_page_count
	.uleb128 0x1d
	.4byte	.LASF257
	.byte	0x10
	.byte	0x69
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_ver_table
	.uleb128 0x1d
	.4byte	.LASF258
	.byte	0x10
	.byte	0x6a
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_region_ppn_table
	.uleb128 0x1d
	.4byte	.LASF259
	.byte	0x10
	.byte	0x6b
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_map_block
	.uleb128 0x1d
	.4byte	.LASF260
	.byte	0x10
	.byte	0x6d
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_table
	.uleb128 0x1d
	.4byte	.LASF261
	.byte	0x10
	.byte	0x6e
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_valid_page_count
	.uleb128 0x1d
	.4byte	.LASF262
	.byte	0x10
	.byte	0x6f
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_ver_table
	.uleb128 0x1d
	.4byte	.LASF263
	.byte	0x10
	.byte	0x70
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_region_ppn_table
	.uleb128 0x1d
	.4byte	.LASF264
	.byte	0x10
	.byte	0x71
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_vendor_block
	.uleb128 0x1d
	.4byte	.LASF265
	.byte	0x10
	.byte	0x73
	.4byte	0x11d7
	.uleb128 0x5
	.byte	0x3
	.4byte	p_l2p_ram_map
	.uleb128 0x8
	.byte	0x4
	.4byte	0x636
	.uleb128 0x1d
	.4byte	.LASF266
	.byte	0x10
	.byte	0x74
	.4byte	0x539
	.uleb128 0x5
	.byte	0x3
	.4byte	p_l2p_map_buf
	.uleb128 0x1d
	.4byte	.LASF267
	.byte	0x10
	.byte	0x75
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_l2p_last_update_region_id
	.uleb128 0x1d
	.4byte	.LASF268
	.byte	0x10
	.byte	0x7c
	.4byte	0x1210
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_table
	.uleb128 0x8
	.byte	0x4
	.4byte	0xaab
	.uleb128 0x1d
	.4byte	.LASF269
	.byte	0x10
	.byte	0x7d
	.4byte	0x1210
	.uleb128 0x5
	.byte	0x3
	.4byte	p_free_data_block_list_head
	.uleb128 0x1d
	.4byte	.LASF270
	.byte	0x10
	.byte	0x7f
	.4byte	0x1210
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_head
	.uleb128 0x1d
	.4byte	.LASF271
	.byte	0x10
	.byte	0x80
	.4byte	0x1210
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_tail
	.uleb128 0x1d
	.4byte	.LASF272
	.byte	0x10
	.byte	0x81
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_num_free_superblocks
	.uleb128 0x1d
	.4byte	.LASF273
	.byte	0x10
	.byte	0x82
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_num_data_superblocks
	.uleb128 0x1d
	.4byte	.LASF274
	.byte	0x10
	.byte	0x83
	.4byte	0xb37
	.uleb128 0x5
	.byte	0x3
	.4byte	g_active_superblock
	.uleb128 0x1d
	.4byte	.LASF275
	.byte	0x10
	.byte	0x84
	.4byte	0xb37
	.uleb128 0x5
	.byte	0x3
	.4byte	g_buffer_superblock
	.uleb128 0x1d
	.4byte	.LASF276
	.byte	0x10
	.byte	0x85
	.4byte	0xb37
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_temp_superblock
	.uleb128 0x1d
	.4byte	.LASF277
	.byte	0x10
	.byte	0x86
	.4byte	0xb37
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_superblock
	.uleb128 0x1d
	.4byte	.LASF278
	.byte	0x10
	.byte	0x87
	.4byte	0x12c0
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_last_act_superblock
	.uleb128 0x8
	.byte	0x4
	.4byte	0xb37
	.uleb128 0x1d
	.4byte	.LASF279
	.byte	0x10
	.byte	0x88
	.4byte	0x858
	.uleb128 0x5
	.byte	0x3
	.4byte	g_sys_save_data
	.uleb128 0x1d
	.4byte	.LASF280
	.byte	0x10
	.byte	0x89
	.4byte	0x955
	.uleb128 0x5
	.byte	0x3
	.4byte	g_sys_ext_data
	.uleb128 0x1d
	.4byte	.LASF281
	.byte	0x10
	.byte	0x8b
	.4byte	0x12f9
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_page_info
	.uleb128 0x8
	.byte	0x4
	.4byte	0xbd9
	.uleb128 0x1d
	.4byte	.LASF282
	.byte	0x10
	.byte	0x8c
	.4byte	0x630
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_blk_tbl
	.uleb128 0x1d
	.4byte	.LASF283
	.byte	0x10
	.byte	0x8d
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_blk_num
	.uleb128 0x1d
	.4byte	.LASF284
	.byte	0x10
	.byte	0x8e
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_page_offset
	.uleb128 0x1d
	.4byte	.LASF285
	.byte	0x10
	.byte	0x8f
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_cur_blk_valid_pages
	.uleb128 0x1d
	.4byte	.LASF286
	.byte	0x10
	.byte	0x90
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_cur_blk_max_valid_pages
	.uleb128 0x1d
	.4byte	.LASF287
	.byte	0x10
	.byte	0x91
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_next_blk
	.uleb128 0x1d
	.4byte	.LASF288
	.byte	0x10
	.byte	0x92
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_next_blk_1
	.uleb128 0x1d
	.4byte	.LASF289
	.byte	0x10
	.byte	0x93
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_temp_num
	.uleb128 0x1d
	.4byte	.LASF290
	.byte	0x10
	.byte	0x94
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_gc_index
	.uleb128 0xc
	.4byte	0x301
	.4byte	0x13a8
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x10
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF291
	.byte	0x10
	.byte	0x95
	.4byte	0x1398
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_temp_tbl
	.uleb128 0x1d
	.4byte	.LASF292
	.byte	0x10
	.byte	0x97
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_free_blk_threshold
	.uleb128 0x1d
	.4byte	.LASF293
	.byte	0x10
	.byte	0x98
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_merge_free_blk_threshold
	.uleb128 0x1d
	.4byte	.LASF294
	.byte	0x10
	.byte	0x99
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_blk_index
	.uleb128 0x1d
	.4byte	.LASF295
	.byte	0x10
	.byte	0x9b
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_gc_page_count
	.uleb128 0x1d
	.4byte	.LASF296
	.byte	0x10
	.byte	0x9c
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_write_page_count
	.uleb128 0x1d
	.4byte	.LASF297
	.byte	0x10
	.byte	0x9d
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_write_sector
	.uleb128 0x1d
	.4byte	.LASF298
	.byte	0x10
	.byte	0x9e
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_read_sector
	.uleb128 0x1d
	.4byte	.LASF299
	.byte	0x10
	.byte	0xa0
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_discard_page_count
	.uleb128 0x1d
	.4byte	.LASF300
	.byte	0x10
	.byte	0xa1
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_read_page_count
	.uleb128 0x1d
	.4byte	.LASF301
	.byte	0x10
	.byte	0xa2
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_tmp_data_superblock_id
	.uleb128 0x1d
	.4byte	.LASF302
	.byte	0x10
	.byte	0xa3
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_cache_write_count
	.uleb128 0x1d
	.4byte	.LASF303
	.byte	0x10
	.byte	0xa4
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_l2p_write_count
	.uleb128 0x1d
	.4byte	.LASF304
	.byte	0x10
	.byte	0xa6
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_mlc_erase_count
	.uleb128 0x1d
	.4byte	.LASF305
	.byte	0x10
	.byte	0xa7
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_avg_erase_count
	.uleb128 0x1d
	.4byte	.LASF306
	.byte	0x10
	.byte	0xa8
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_min_erase_count
	.uleb128 0x1d
	.4byte	.LASF307
	.byte	0x10
	.byte	0xa9
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_slc_erase_count
	.uleb128 0x1d
	.4byte	.LASF308
	.byte	0x10
	.byte	0xaa
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_sys_slc_erase_count
	.uleb128 0x1d
	.4byte	.LASF309
	.byte	0x10
	.byte	0xab
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_max_erase_count
	.uleb128 0x1d
	.4byte	.LASF310
	.byte	0x10
	.byte	0xac
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_in_gc_progress
	.uleb128 0x1d
	.4byte	.LASF311
	.byte	0x10
	.byte	0xad
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_in_swl_replace
	.uleb128 0x1d
	.4byte	.LASF312
	.byte	0x10
	.byte	0xae
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ftl_nand_free_count
	.uleb128 0x1d
	.4byte	.LASF313
	.byte	0x10
	.byte	0xaf
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_head_data_block
	.uleb128 0x1d
	.4byte	.LASF314
	.byte	0x10
	.byte	0xb0
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_head_data_block_count
	.uleb128 0x1d
	.4byte	.LASF315
	.byte	0x10
	.byte	0xb1
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_skip_write_count
	.uleb128 0x1d
	.4byte	.LASF316
	.byte	0x10
	.byte	0xb2
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_cur_erase_blk
	.uleb128 0x1d
	.4byte	.LASF317
	.byte	0x10
	.byte	0xb4
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_page_num
	.uleb128 0x1d
	.4byte	.LASF318
	.byte	0x10
	.byte	0xb5
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_page_min_ver
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x1594
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1f
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF319
	.byte	0x10
	.byte	0xb6
	.4byte	0x1584
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_ppa_tbl
	.uleb128 0x1d
	.4byte	.LASF320
	.byte	0x10
	.byte	0xb7
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	c_mlc_erase_count_value
	.uleb128 0x1d
	.4byte	.LASF321
	.byte	0x10
	.byte	0xb8
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_power_lost_recovery_flag
	.uleb128 0x9
	.4byte	.LASF322
	.byte	0xd
	.byte	0x14
	.4byte	0x37c
	.uleb128 0x9
	.4byte	.LASF323
	.byte	0xd
	.byte	0x15
	.4byte	0x37c
	.uleb128 0x9
	.4byte	.LASF324
	.byte	0xd
	.byte	0x16
	.4byte	0x41e
	.uleb128 0xc
	.4byte	0x99
	.4byte	0x15f9
	.uleb128 0x19
	.4byte	0x2e
	.2byte	0x3ff
	.byte	0
	.uleb128 0x9
	.4byte	.LASF325
	.byte	0xd
	.byte	0x17
	.4byte	0x15e8
	.uleb128 0x1d
	.4byte	.LASF326
	.byte	0x3
	.byte	0x5
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	DeviceCapacity
	.uleb128 0x1d
	.4byte	.LASF327
	.byte	0x3
	.byte	0x6
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	gFtlInitStatus
	.uleb128 0x9
	.4byte	.LASF328
	.byte	0x3
	.byte	0x6d
	.4byte	0x30c
	.uleb128 0x9
	.4byte	.LASF329
	.byte	0x3
	.byte	0x6e
	.4byte	0x30c
	.uleb128 0x9
	.4byte	.LASF330
	.byte	0x3
	.byte	0x6f
	.4byte	0x30c
	.uleb128 0x9
	.4byte	.LASF331
	.byte	0x3
	.byte	0x70
	.4byte	0x30c
	.uleb128 0x1e
	.4byte	.LASF332
	.byte	0x3
	.2byte	0x3bd
	.4byte	0x64
	.uleb128 0x5
	.byte	0x3
	.4byte	power_up_flag
	.uleb128 0x1f
	.4byte	.LASF336
	.byte	0x3
	.2byte	0x480
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_discard_updated
	.uleb128 0x1e
	.4byte	.LASF333
	.byte	0x2
	.2byte	0x35b
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_power_up_flush
	.uleb128 0x1e
	.4byte	.LASF334
	.byte	0x2
	.2byte	0x370
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	FtlUpdateVaildLpnCount
	.uleb128 0xc
	.4byte	0x301
	.4byte	0x16ab
	.uleb128 0x19
	.4byte	0x2e
	.2byte	0x1fff
	.byte	0
	.uleb128 0x1e
	.4byte	.LASF335
	.byte	0x2
	.2byte	0x8ad
	.4byte	0x169a
	.uleb128 0x5
	.byte	0x3
	.4byte	check_vpc_table
	.uleb128 0x20
	.4byte	.LASF337
	.byte	0x5
	.byte	0x6d
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_gc_temp_block_bops_scan_page_addr
	.uleb128 0x1e
	.4byte	.LASF338
	.byte	0x5
	.2byte	0x1d4
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_ink_free_return_value
	.uleb128 0x20
	.4byte	.LASF339
	.byte	0x1
	.byte	0xe
	.4byte	0x15e8
	.uleb128 0x5
	.byte	0x3
	.4byte	check_buf
	.uleb128 0x21
	.4byte	0x15f9
	.byte	0x1
	.byte	0xf
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_temp_buf
	.uleb128 0xc
	.4byte	0x99
	.4byte	0x170e
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x7f
	.byte	0
	.uleb128 0x20
	.4byte	.LASF340
	.byte	0x1
	.byte	0x10
	.4byte	0x16fe
	.uleb128 0x5
	.byte	0x3
	.4byte	check_spare_buf
	.uleb128 0x22
	.4byte	.LASF343
	.byte	0x1
	.byte	0xd2
	.4byte	0x64
	.4byte	.LFB279
	.4byte	.LFE279-.LFB279
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LASF341
	.byte	0x1
	.byte	0xd2
	.4byte	0x25
	.4byte	.LLST300
	.uleb128 0x23
	.4byte	.LASF342
	.byte	0x1
	.byte	0xd2
	.4byte	0x25
	.4byte	.LLST301
	.uleb128 0x23
	.4byte	.LASF114
	.byte	0x1
	.byte	0xd2
	.4byte	0x41
	.4byte	.LLST302
	.uleb128 0x24
	.4byte	.LVL1631
	.4byte	0x6700
	.byte	0
	.uleb128 0x22
	.4byte	.LASF344
	.byte	0x1
	.byte	0xcd
	.4byte	0x25
	.4byte	.LFB278
	.4byte	.LFE278-.LFB278
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x17bf
	.uleb128 0x23
	.4byte	.LASF345
	.byte	0x1
	.byte	0xcd
	.4byte	0x25
	.4byte	.LLST176
	.uleb128 0x23
	.4byte	.LASF346
	.byte	0x1
	.byte	0xcd
	.4byte	0x17bf
	.4byte	.LLST177
	.uleb128 0x23
	.4byte	.LASF347
	.byte	0x1
	.byte	0xcd
	.4byte	0x41
	.4byte	.LLST178
	.uleb128 0x24
	.4byte	.LVL813
	.4byte	0x670b
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x17c5
	.uleb128 0x25
	.uleb128 0x26
	.4byte	.LASF350
	.byte	0x1
	.byte	0xc8
	.4byte	0x25
	.byte	0x1
	.4byte	0x180a
	.uleb128 0x27
	.ascii	"s\000"
	.byte	0x1
	.byte	0xc8
	.4byte	0x25
	.uleb128 0x27
	.ascii	"c\000"
	.byte	0x1
	.byte	0xc8
	.4byte	0x64
	.uleb128 0x27
	.ascii	"n\000"
	.byte	0x1
	.byte	0xc8
	.4byte	0x41
	.uleb128 0x28
	.uleb128 0x29
	.ascii	"__p\000"
	.byte	0x1
	.byte	0xca
	.4byte	0x25
	.uleb128 0x29
	.ascii	"__n\000"
	.byte	0x1
	.byte	0xca
	.4byte	0xce
	.byte	0
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF348
	.byte	0x1
	.byte	0xb6
	.4byte	0x8e
	.4byte	.LFB276
	.4byte	.LFE276-.LFB276
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1900
	.uleb128 0x23
	.4byte	.LASF349
	.byte	0x1
	.byte	0xb6
	.4byte	0x46f
	.4byte	.LLST108
	.uleb128 0x2b
	.ascii	"die\000"
	.byte	0x1
	.byte	0xb6
	.4byte	0x99
	.4byte	.LLST109
	.uleb128 0x2c
	.4byte	.LASF114
	.byte	0x1
	.byte	0xb8
	.4byte	0x83
	.4byte	.LLST110
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x1
	.byte	0xb8
	.4byte	0x83
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB337
	.4byte	.Ldebug_ranges0+0x100
	.byte	0x1
	.byte	0xba
	.4byte	0x18aa
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x100
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x100
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL468
	.4byte	0x6714
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x32
	.4byte	0x17c6
	.4byte	.LBB346
	.4byte	.LBE346-.LBB346
	.byte	0x1
	.byte	0xbd
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB347
	.4byte	.LBE347-.LBB347
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB348
	.4byte	.LBE348-.LBB348
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL472
	.4byte	0x6714
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x26
	.4byte	.LASF351
	.byte	0x1
	.byte	0x9b
	.4byte	0x317
	.byte	0x1
	.4byte	0x1944
	.uleb128 0x34
	.4byte	.LASF352
	.byte	0x1
	.byte	0x9b
	.4byte	0x301
	.uleb128 0x29
	.ascii	"sts\000"
	.byte	0x1
	.byte	0x9d
	.4byte	0x317
	.uleb128 0x35
	.4byte	.LASF353
	.byte	0x1
	.byte	0x9e
	.4byte	0x1944
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x9f
	.4byte	0x32d
	.uleb128 0x36
	.ascii	"out\000"
	.byte	0x1
	.byte	0xb1
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x1954
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xf
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF354
	.byte	0x1
	.byte	0x85
	.4byte	0x8e
	.4byte	.LFB274
	.4byte	.LFE274-.LFB274
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LASF355
	.byte	0x1
	.byte	0x85
	.4byte	0x25
	.4byte	.LLST95
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x85
	.4byte	0x99
	.4byte	.LLST96
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x85
	.4byte	0x99
	.4byte	.LLST97
	.uleb128 0x37
	.ascii	"i\000"
	.byte	0x1
	.byte	0x87
	.4byte	0x99
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x87
	.4byte	0x99
	.uleb128 0x35
	.4byte	.LASF61
	.byte	0x1
	.byte	0x87
	.4byte	0x99
	.uleb128 0x20
	.4byte	.LASF358
	.byte	0x1
	.byte	0x87
	.4byte	0x99
	.uleb128 0x2
	.byte	0x91
	.sleb128 -36
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x88
	.4byte	0xf40
	.uleb128 0x20
	.4byte	.LASF359
	.byte	0x1
	.byte	0x89
	.4byte	0x99
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x31
	.4byte	.LVL407
	.4byte	0x1ba6
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF360
	.byte	0x1
	.byte	0x49
	.4byte	0x8e
	.4byte	.LFB273
	.4byte	.LFE273-.LFB273
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ae3
	.uleb128 0x23
	.4byte	.LASF355
	.byte	0x1
	.byte	0x49
	.4byte	0x25
	.4byte	.LLST90
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST91
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST92
	.uleb128 0x23
	.4byte	.LASF361
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST93
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x1
	.byte	0x4b
	.4byte	0x99
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x4b
	.4byte	0x99
	.uleb128 0x35
	.4byte	.LASF61
	.byte	0x1
	.byte	0x4b
	.4byte	0x99
	.uleb128 0x20
	.4byte	.LASF358
	.byte	0x1
	.byte	0x4b
	.4byte	0x99
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x2c
	.4byte	.LASF359
	.byte	0x1
	.byte	0x4c
	.4byte	0x99
	.4byte	.LLST94
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x4d
	.4byte	0xf40
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7595
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0xb0
	.4byte	0x1ad0
	.uleb128 0x20
	.4byte	.LASF363
	.byte	0x1
	.byte	0x68
	.4byte	0x32d
	.uleb128 0x2
	.byte	0x91
	.sleb128 -60
	.uleb128 0x31
	.4byte	.LVL391
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL393
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL394
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL395
	.4byte	0x671f
	.byte	0
	.uleb128 0x31
	.4byte	.LVL385
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL386
	.4byte	0x1ba6
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x1af3
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xe
	.byte	0
	.uleb128 0x4
	.4byte	0x1ae3
	.uleb128 0x2a
	.4byte	.LASF364
	.byte	0x1
	.byte	0x2b
	.4byte	0x8e
	.4byte	.LFB272
	.4byte	.LFE272-.LFB272
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ba6
	.uleb128 0x23
	.4byte	.LASF355
	.byte	0x1
	.byte	0x2b
	.4byte	0x25
	.4byte	.LLST83
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x2b
	.4byte	0x99
	.4byte	.LLST84
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x2b
	.4byte	0x99
	.4byte	.LLST85
	.uleb128 0x37
	.ascii	"i\000"
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x35
	.4byte	.LASF61
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x20
	.4byte	.LASF358
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x20
	.4byte	.LASF359
	.byte	0x1
	.byte	0x2e
	.4byte	0x99
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x2f
	.4byte	0xf40
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7578
	.uleb128 0x31
	.4byte	.LVL355
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL358
	.4byte	0x1ba6
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF668
	.byte	0x1
	.byte	0x11
	.4byte	0x99
	.4byte	.LFB271
	.4byte	.LFE271-.LFB271
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c4e
	.uleb128 0x2b
	.ascii	"req\000"
	.byte	0x1
	.byte	0x11
	.4byte	0xf40
	.4byte	.LLST0
	.uleb128 0x23
	.4byte	.LASF358
	.byte	0x1
	.byte	0x11
	.4byte	0x376
	.4byte	.LLST1
	.uleb128 0x23
	.4byte	.LASF365
	.byte	0x1
	.byte	0x11
	.4byte	0x376
	.4byte	.LLST2
	.uleb128 0x35
	.4byte	.LASF366
	.byte	0x1
	.byte	0x13
	.4byte	0x83
	.uleb128 0x35
	.4byte	.LASF367
	.byte	0x1
	.byte	0x13
	.4byte	0x83
	.uleb128 0x2c
	.4byte	.LASF71
	.byte	0x1
	.byte	0x14
	.4byte	0x83
	.4byte	.LLST3
	.uleb128 0x2c
	.4byte	.LASF72
	.byte	0x1
	.byte	0x14
	.4byte	0x83
	.4byte	.LLST4
	.uleb128 0x2c
	.4byte	.LASF368
	.byte	0x1
	.byte	0x15
	.4byte	0x99
	.4byte	.LLST5
	.uleb128 0x2c
	.4byte	.LASF62
	.byte	0x1
	.byte	0x16
	.4byte	0x99
	.4byte	.LLST6
	.uleb128 0x2c
	.4byte	.LASF369
	.byte	0x1
	.byte	0x17
	.4byte	0x99
	.4byte	.LLST7
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF370
	.byte	0x5
	.2byte	0x2f8
	.4byte	0x99
	.4byte	.LFB270
	.4byte	.LFE270-.LFB270
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c72
	.uleb128 0x24
	.4byte	.LVL1370
	.4byte	0x1c72
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF371
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x64
	.byte	0x1
	.4byte	0x1d3f
	.uleb128 0x3d
	.4byte	.LASF372
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF373
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF374
	.byte	0x5
	.2byte	0x1d7
	.4byte	0x301
	.uleb128 0x3f
	.ascii	"lpa\000"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x30c
	.uleb128 0x3f
	.ascii	"ppa\000"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF375
	.byte	0x5
	.2byte	0x1d9
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF85
	.byte	0x5
	.2byte	0x1da
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x5
	.2byte	0x1da
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF377
	.byte	0x5
	.2byte	0x1da
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x5
	.2byte	0x1da
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF379
	.byte	0x5
	.2byte	0x1db
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x5
	.2byte	0x1dc
	.4byte	0x1d3f
	.uleb128 0x40
	.4byte	.LASF381
	.byte	0x5
	.2byte	0x276
	.uleb128 0x40
	.4byte	.LASF382
	.byte	0x5
	.2byte	0x279
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x1d55
	.4byte	.LASF371
	.uleb128 0x28
	.uleb128 0x3e
	.4byte	.LASF383
	.byte	0x5
	.2byte	0x1f3
	.4byte	0x301
	.byte	0
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x770
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x1d55
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x16
	.byte	0
	.uleb128 0x4
	.4byte	0x1d45
	.uleb128 0x3c
	.4byte	.LASF384
	.byte	0x5
	.2byte	0x1a2
	.4byte	0x64
	.byte	0x1
	.4byte	0x1dba
	.uleb128 0x3f
	.ascii	"ret\000"
	.byte	0x5
	.2byte	0x1a4
	.4byte	0x64
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x1a5
	.4byte	0x64
	.uleb128 0x3f
	.ascii	"ppa\000"
	.byte	0x5
	.2byte	0x1a6
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF353
	.byte	0x5
	.2byte	0x1a7
	.4byte	0x1944
	.uleb128 0x42
	.4byte	0x1dab
	.uleb128 0x3e
	.4byte	.LASF385
	.byte	0x5
	.2byte	0x1b1
	.4byte	0x32d
	.byte	0
	.uleb128 0x28
	.uleb128 0x3e
	.4byte	.LASF386
	.byte	0x5
	.2byte	0x1c6
	.4byte	0x30c
	.byte	0
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF387
	.byte	0x5
	.2byte	0x185
	.4byte	0x64
	.byte	0x1
	.4byte	0x1e04
	.uleb128 0x3d
	.4byte	.LASF388
	.byte	0x5
	.2byte	0x185
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x5
	.2byte	0x187
	.4byte	0x301
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x188
	.4byte	0x301
	.uleb128 0x3f
	.ascii	"j\000"
	.byte	0x5
	.2byte	0x188
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x5
	.2byte	0x189
	.4byte	0x301
	.byte	0
	.uleb128 0x43
	.4byte	.LASF497
	.byte	0x5
	.2byte	0x17a
	.4byte	0x64
	.byte	0x1
	.uleb128 0x3b
	.4byte	.LASF389
	.byte	0x5
	.2byte	0x166
	.4byte	0x64
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1e6d
	.uleb128 0x44
	.4byte	.LASF178
	.byte	0x5
	.2byte	0x166
	.4byte	0x301
	.4byte	.LLST82
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x168
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF390
	.byte	0x5
	.2byte	0x169
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL341
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL342
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL343
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF391
	.byte	0x5
	.2byte	0x158
	.4byte	0x64
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ea1
	.uleb128 0x44
	.4byte	.LASF392
	.byte	0x5
	.2byte	0x158
	.4byte	0x301
	.4byte	.LLST81
	.uleb128 0x31
	.4byte	.LVL338
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF393
	.byte	0x5
	.2byte	0x13b
	.4byte	0x30c
	.4byte	.LFB263
	.4byte	.LFE263-.LFB263
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1f15
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x13d
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF380
	.byte	0x5
	.2byte	0x13e
	.4byte	0x1d3f
	.4byte	.LLST254
	.uleb128 0x31
	.4byte	.LVL1271
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1272
	.4byte	0x22cb
	.uleb128 0x31
	.4byte	.LVL1273
	.4byte	0x20c4
	.uleb128 0x31
	.4byte	.LVL1274
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1275
	.4byte	0x499b
	.uleb128 0x31
	.4byte	.LVL1276
	.4byte	0x2093
	.uleb128 0x31
	.4byte	.LVL1278
	.4byte	0x21ed
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF394
	.byte	0x5
	.2byte	0x12b
	.4byte	0x30c
	.4byte	.LFB262
	.4byte	.LFE262-.LFB262
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1f6f
	.uleb128 0x31
	.4byte	.LVL1279
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1280
	.4byte	0x20c4
	.uleb128 0x31
	.4byte	.LVL1281
	.4byte	0x2505
	.uleb128 0x31
	.4byte	.LVL1282
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1283
	.4byte	0x372d
	.uleb128 0x31
	.4byte	.LVL1284
	.4byte	0x3588
	.uleb128 0x24
	.4byte	.LVL1285
	.4byte	0x23e4
	.byte	0
	.uleb128 0x46
	.4byte	.LASF453
	.byte	0x5
	.2byte	0x120
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF395
	.byte	0x5
	.byte	0xd2
	.4byte	0x30c
	.4byte	.LFB260
	.4byte	.LFE260-.LFB260
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2093
	.uleb128 0x23
	.4byte	.LASF396
	.byte	0x5
	.byte	0xd2
	.4byte	0x12c0
	.4byte	.LLST167
	.uleb128 0x23
	.4byte	.LASF397
	.byte	0x5
	.byte	0xd2
	.4byte	0x30c
	.4byte	.LLST168
	.uleb128 0x2c
	.4byte	.LASF378
	.byte	0x5
	.byte	0xd4
	.4byte	0x301
	.4byte	.LLST169
	.uleb128 0x2c
	.4byte	.LASF357
	.byte	0x5
	.byte	0xd5
	.4byte	0x301
	.4byte	.LLST170
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x5
	.byte	0xd5
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF376
	.byte	0x5
	.byte	0xd6
	.4byte	0x301
	.uleb128 0x2c
	.4byte	.LASF172
	.byte	0x5
	.byte	0xd6
	.4byte	0x301
	.4byte	.LLST171
	.uleb128 0x2c
	.4byte	.LASF170
	.byte	0x5
	.byte	0xd6
	.4byte	0x301
	.4byte	.LLST172
	.uleb128 0x2c
	.4byte	.LASF398
	.byte	0x5
	.byte	0xd7
	.4byte	0x30c
	.4byte	.LLST173
	.uleb128 0x2c
	.4byte	.LASF399
	.byte	0x5
	.byte	0xd8
	.4byte	0x30c
	.4byte	.LLST174
	.uleb128 0x35
	.4byte	.LASF400
	.byte	0x5
	.byte	0xd9
	.4byte	0x30c
	.uleb128 0x2c
	.4byte	.LASF380
	.byte	0x5
	.byte	0xda
	.4byte	0x1d3f
	.4byte	.LLST175
	.uleb128 0x47
	.4byte	.LASF401
	.byte	0x5
	.byte	0xe1
	.uleb128 0x47
	.4byte	.LASF402
	.byte	0x5
	.byte	0xe5
	.uleb128 0x48
	.4byte	.LASF471
	.byte	0x5
	.2byte	0x117
	.4byte	.L976
	.uleb128 0x31
	.4byte	.LVL773
	.4byte	0x2093
	.uleb128 0x31
	.4byte	.LVL781
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL795
	.4byte	0x4595
	.uleb128 0x31
	.4byte	.LVL803
	.4byte	0x313e
	.uleb128 0x31
	.4byte	.LVL805
	.4byte	0x21ed
	.uleb128 0x31
	.4byte	.LVL807
	.4byte	0x422b
	.uleb128 0x31
	.4byte	.LVL808
	.4byte	0x2093
	.byte	0
	.uleb128 0x49
	.4byte	.LASF409
	.byte	0x5
	.byte	0xc9
	.4byte	.LFB259
	.4byte	.LFE259-.LFB259
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x20c4
	.uleb128 0x31
	.4byte	.LVL767
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL768
	.4byte	0x17c6
	.uleb128 0x24
	.4byte	.LVL769
	.4byte	0x230f
	.byte	0
	.uleb128 0x22
	.4byte	.LASF403
	.byte	0x5
	.byte	0x6f
	.4byte	0x64
	.4byte	.LFB258
	.4byte	.LFE258-.LFB258
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x21d8
	.uleb128 0x23
	.4byte	.LASF404
	.byte	0x5
	.byte	0x6f
	.4byte	0x64
	.4byte	.LLST250
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x71
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF405
	.byte	0x5
	.byte	0x71
	.4byte	0x301
	.uleb128 0x2c
	.4byte	.LASF406
	.byte	0x5
	.byte	0x72
	.4byte	0x30c
	.4byte	.LLST251
	.uleb128 0x2c
	.4byte	.LASF407
	.byte	0x5
	.byte	0x73
	.4byte	0x30c
	.4byte	.LLST252
	.uleb128 0x35
	.4byte	.LASF396
	.byte	0x5
	.byte	0x74
	.4byte	0x12c0
	.uleb128 0x2c
	.4byte	.LASF408
	.byte	0x5
	.byte	0x75
	.4byte	0x12f9
	.4byte	.LLST253
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x21e8
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7428
	.uleb128 0x31
	.4byte	.LVL1239
	.4byte	0x1f78
	.uleb128 0x31
	.4byte	.LVL1241
	.4byte	0x372d
	.uleb128 0x31
	.4byte	.LVL1242
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1243
	.4byte	0x499b
	.uleb128 0x31
	.4byte	.LVL1247
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1248
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1250
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1251
	.4byte	0x4206
	.uleb128 0x31
	.4byte	.LVL1253
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1254
	.4byte	0x3588
	.uleb128 0x31
	.4byte	.LVL1256
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1257
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1258
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1259
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1260
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1261
	.4byte	0x422b
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x21e8
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x12
	.byte	0
	.uleb128 0x4
	.4byte	0x21d8
	.uleb128 0x49
	.4byte	.LASF410
	.byte	0x5
	.byte	0x54
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2255
	.uleb128 0x23
	.4byte	.LASF411
	.byte	0x5
	.byte	0x54
	.4byte	0x30c
	.4byte	.LLST76
	.uleb128 0x23
	.4byte	.LASF181
	.byte	0x5
	.byte	0x54
	.4byte	0x30c
	.4byte	.LLST77
	.uleb128 0x2b
	.ascii	"lpa\000"
	.byte	0x5
	.byte	0x54
	.4byte	0x30c
	.4byte	.LLST78
	.uleb128 0x2c
	.4byte	.LASF412
	.byte	0x5
	.byte	0x56
	.4byte	0x301
	.4byte	.LLST79
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x5
	.byte	0x57
	.4byte	0x301
	.4byte	.LLST80
	.uleb128 0x31
	.4byte	.LVL326
	.4byte	0x4561
	.byte	0
	.uleb128 0x22
	.4byte	.LASF413
	.byte	0x5
	.byte	0x49
	.4byte	0x30c
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2287
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x5
	.byte	0x49
	.4byte	0x301
	.4byte	.LLST75
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x4b
	.4byte	0x301
	.byte	0
	.uleb128 0x49
	.4byte	.LASF414
	.byte	0x5
	.byte	0x38
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x22cb
	.uleb128 0x4b
	.ascii	"req\000"
	.byte	0x5
	.byte	0x38
	.4byte	0xf40
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x4c
	.4byte	.LASF415
	.byte	0x5
	.byte	0x38
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x3a
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF416
	.byte	0x5
	.byte	0x3b
	.4byte	0x301
	.byte	0
	.uleb128 0x49
	.4byte	.LASF417
	.byte	0x5
	.byte	0x29
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x230f
	.uleb128 0x4b
	.ascii	"req\000"
	.byte	0x5
	.byte	0x29
	.4byte	0xf40
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x4c
	.4byte	.LASF415
	.byte	0x5
	.byte	0x29
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x2b
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF416
	.byte	0x5
	.byte	0x2c
	.4byte	0x301
	.byte	0
	.uleb128 0x49
	.4byte	.LASF418
	.byte	0x5
	.byte	0x15
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x232e
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x17
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF419
	.byte	0x2
	.2byte	0xa93
	.4byte	0x64
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x238f
	.uleb128 0x44
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa93
	.4byte	0x301
	.4byte	.LLST191
	.uleb128 0x3f
	.ascii	"ret\000"
	.byte	0x2
	.2byte	0xa95
	.4byte	0x64
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x239f
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7358
	.uleb128 0x31
	.4byte	.LVL886
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL888
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL890
	.4byte	0x23a4
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x239f
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x13
	.byte	0
	.uleb128 0x4
	.4byte	0x238f
	.uleb128 0x3c
	.4byte	.LASF420
	.byte	0x2
	.2byte	0xa7e
	.4byte	0x64
	.byte	0x1
	.4byte	0x23cf
	.uleb128 0x3d
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa7e
	.4byte	0x301
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x23df
	.4byte	.LASF420
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x23df
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xf
	.byte	0
	.uleb128 0x4
	.4byte	0x23cf
	.uleb128 0x3b
	.4byte	.LASF421
	.byte	0x2
	.2byte	0xa5e
	.4byte	0x30c
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2460
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa5e
	.4byte	0x12c0
	.4byte	.LLST73
	.uleb128 0x45
	.4byte	.LASF378
	.byte	0x2
	.2byte	0xa60
	.4byte	0x301
	.4byte	.LLST74
	.uleb128 0x1f
	.4byte	.LASF423
	.byte	0x2
	.2byte	0xa61
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x21e8
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7338
	.uleb128 0x31
	.4byte	.LVL311
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL312
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL313
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL316
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF424
	.byte	0x2
	.2byte	0xa46
	.4byte	0x64
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x24f0
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa46
	.4byte	0x12c0
	.4byte	.LLST236
	.uleb128 0x1f
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa48
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x2500
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7331
	.uleb128 0x31
	.4byte	.LVL1171
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1172
	.4byte	0x4206
	.uleb128 0x31
	.4byte	.LVL1173
	.4byte	0x23a4
	.uleb128 0x31
	.4byte	.LVL1174
	.4byte	0x2505
	.uleb128 0x31
	.4byte	.LVL1175
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1176
	.4byte	0x372d
	.uleb128 0x31
	.4byte	.LVL1177
	.4byte	0x3588
	.uleb128 0x31
	.4byte	.LVL1179
	.4byte	0x422b
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2500
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1c
	.byte	0
	.uleb128 0x4
	.4byte	0x24f0
	.uleb128 0x4d
	.4byte	.LASF425
	.byte	0x2
	.2byte	0x9e2
	.4byte	0x64
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x265f
	.uleb128 0x4e
	.ascii	"p\000"
	.byte	0x2
	.2byte	0x9e2
	.4byte	0x12c0
	.4byte	.LLST183
	.uleb128 0x45
	.4byte	.LASF426
	.byte	0x2
	.2byte	0x9e4
	.4byte	0x301
	.4byte	.LLST184
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x301
	.4byte	.LLST185
	.uleb128 0x3f
	.ascii	"n\000"
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF427
	.byte	0x2
	.2byte	0x9e6
	.4byte	0x64
	.4byte	.LLST186
	.uleb128 0x45
	.4byte	.LASF428
	.byte	0x2
	.2byte	0x9e7
	.4byte	0x301
	.4byte	.LLST187
	.uleb128 0x40
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x9e8
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x266f
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7310
	.uleb128 0x4f
	.4byte	0x3409
	.4byte	.LBB428
	.4byte	.Ldebug_ranges0+0x188
	.byte	0x2
	.2byte	0xa18
	.4byte	0x25ce
	.uleb128 0x2e
	.4byte	0x3422
	.uleb128 0x2e
	.4byte	0x3416
	.uleb128 0x31
	.4byte	.LVL858
	.4byte	0x5c75
	.byte	0
	.uleb128 0x31
	.4byte	.LVL834
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL838
	.4byte	0x4027
	.uleb128 0x31
	.4byte	.LVL839
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL840
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL841
	.4byte	0x4206
	.uleb128 0x31
	.4byte	.LVL842
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL845
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL846
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL847
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL848
	.4byte	0x3409
	.uleb128 0x31
	.4byte	.LVL850
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL852
	.4byte	0x27a4
	.uleb128 0x31
	.4byte	.LVL853
	.4byte	0x499b
	.uleb128 0x31
	.4byte	.LVL855
	.4byte	0x4206
	.uleb128 0x31
	.4byte	.LVL860
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL863
	.4byte	0x671f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x266f
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x18
	.byte	0
	.uleb128 0x4
	.4byte	0x265f
	.uleb128 0x3c
	.4byte	.LASF430
	.byte	0x2
	.2byte	0x9d9
	.4byte	0x64
	.byte	0x1
	.4byte	0x2692
	.uleb128 0x3d
	.4byte	.LASF405
	.byte	0x2
	.2byte	0x9d9
	.4byte	0x301
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF431
	.byte	0x2
	.2byte	0x98e
	.4byte	0x301
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x272c
	.uleb128 0x1f
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x990
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x3e
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x991
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x992
	.4byte	0x301
	.4byte	.LLST68
	.uleb128 0x45
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x993
	.4byte	0x1210
	.4byte	.LLST69
	.uleb128 0x45
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x994
	.4byte	0x301
	.4byte	.LLST70
	.uleb128 0x45
	.4byte	.LASF437
	.byte	0x2
	.2byte	0x995
	.4byte	0x301
	.4byte	.LLST71
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x996
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL293
	.4byte	0x272c
	.uleb128 0x31
	.4byte	.LVL299
	.4byte	0x277b
	.uleb128 0x31
	.4byte	.LVL303
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF438
	.byte	0x2
	.2byte	0x979
	.4byte	0x301
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x277b
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x979
	.4byte	0x301
	.4byte	.LLST63
	.uleb128 0x1f
	.4byte	.LASF439
	.byte	0x2
	.2byte	0x97b
	.4byte	0x301
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x3e
	.4byte	.LASF392
	.byte	0x2
	.2byte	0x97c
	.4byte	0x301
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x97d
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF440
	.byte	0x2
	.2byte	0x970
	.4byte	0x301
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x27a4
	.uleb128 0x1f
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x972
	.4byte	0x301
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF441
	.byte	0x2
	.2byte	0x960
	.4byte	0x64
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2805
	.uleb128 0x44
	.4byte	.LASF405
	.byte	0x2
	.2byte	0x960
	.4byte	0x301
	.4byte	.LLST62
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x962
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x962
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF165
	.byte	0x2
	.2byte	0x963
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL200
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL201
	.4byte	0x4afe
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF442
	.byte	0x2
	.2byte	0x901
	.4byte	0x64
	.byte	0x1
	.4byte	0x282f
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x903
	.4byte	0x64
	.uleb128 0x28
	.uleb128 0x3e
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x928
	.4byte	0x12c0
	.byte	0
	.byte	0
	.uleb128 0x50
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x8e2
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x289a
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8e4
	.4byte	0x64
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x8e5
	.4byte	0x30c
	.uleb128 0x2
	.byte	0x91
	.sleb128 -28
	.uleb128 0x3e
	.4byte	.LASF444
	.byte	0x2
	.2byte	0x8e6
	.4byte	0x1d3f
	.uleb128 0x31
	.4byte	.LVL634
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL636
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL637
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL638
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL639
	.4byte	0x671f
	.byte	0
	.uleb128 0x50
	.4byte	.LASF445
	.byte	0x2
	.2byte	0x8ae
	.4byte	.LFB240
	.4byte	.LFE240-.LFB240
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x29b3
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8b0
	.4byte	0x301
	.uleb128 0x52
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x8b1
	.4byte	0x30c
	.4byte	.LLST148
	.uleb128 0x1f
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x8b2
	.4byte	0x30c
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x3f
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x8b3
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x8b4
	.4byte	0x301
	.4byte	.LLST149
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x29c3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7226
	.uleb128 0x53
	.4byte	.LBB389
	.4byte	.LBE389-.LBB389
	.4byte	0x292b
	.uleb128 0x45
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x8d1
	.4byte	0x301
	.4byte	.LLST150
	.uleb128 0x31
	.4byte	.LVL654
	.4byte	0x671f
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB386
	.4byte	.LBE386-.LBB386
	.byte	0x2
	.2byte	0x8b7
	.4byte	0x2985
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB387
	.4byte	.LBE387-.LBB387
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB388
	.4byte	.LBE388-.LBB388
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL650
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL649
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL655
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL657
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL658
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL660
	.4byte	0x671f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x29c3
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xd
	.byte	0
	.uleb128 0x4
	.4byte	0x29b3
	.uleb128 0x50
	.4byte	.LASF448
	.byte	0x2
	.2byte	0x88f
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2a6f
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x891
	.4byte	0x301
	.4byte	.LLST234
	.uleb128 0x52
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x892
	.4byte	0x30c
	.4byte	.LLST235
	.uleb128 0x1f
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x893
	.4byte	0x30c
	.uleb128 0x2
	.byte	0x91
	.sleb128 -36
	.uleb128 0x3f
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x894
	.4byte	0x301
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x2a7f
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7210
	.uleb128 0x31
	.4byte	.LVL1157
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1158
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1162
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1163
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1165
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1166
	.4byte	0x23a4
	.uleb128 0x31
	.4byte	.LVL1167
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1168
	.4byte	0x3588
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2a7f
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x14
	.byte	0
	.uleb128 0x4
	.4byte	0x2a6f
	.uleb128 0x50
	.4byte	.LASF449
	.byte	0x2
	.2byte	0x86b
	.4byte	.LFB238
	.4byte	.LFE238-.LFB238
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2af9
	.uleb128 0x44
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x86b
	.4byte	0x12c0
	.4byte	.LLST212
	.uleb128 0x1f
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x86d
	.4byte	0x32d
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x86e
	.4byte	0x1d3f
	.uleb128 0x3e
	.4byte	.LASF451
	.byte	0x2
	.2byte	0x86f
	.4byte	0x64
	.uleb128 0x3e
	.4byte	.LASF174
	.byte	0x2
	.2byte	0x870
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL1036
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL1037
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1038
	.4byte	0x232e
	.byte	0
	.uleb128 0x55
	.4byte	.LASF452
	.byte	0x2
	.2byte	0x851
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b73
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB486
	.4byte	.LBE486-.LBB486
	.byte	0x2
	.2byte	0x855
	.4byte	0x2b69
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB487
	.4byte	.LBE487-.LBB487
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB488
	.4byte	.LBE488-.LBB488
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL1139
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1138
	.4byte	0x377a
	.byte	0
	.uleb128 0x46
	.4byte	.LASF454
	.byte	0x2
	.2byte	0x83d
	.byte	0x1
	.uleb128 0x4d
	.4byte	.LASF455
	.byte	0x2
	.2byte	0x812
	.4byte	0x64
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2c30
	.uleb128 0x45
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x814
	.4byte	0x301
	.4byte	.LLST164
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x814
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x814
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF165
	.byte	0x2
	.2byte	0x814
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF456
	.byte	0x2
	.2byte	0x815
	.4byte	0x301
	.4byte	.LLST165
	.uleb128 0x45
	.4byte	.LASF457
	.byte	0x2
	.2byte	0x815
	.4byte	0x301
	.4byte	.LLST166
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x2c40
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7185
	.uleb128 0x31
	.4byte	.LVL753
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL755
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL756
	.4byte	0x4afe
	.uleb128 0x31
	.4byte	.LVL760
	.4byte	0x422b
	.uleb128 0x31
	.4byte	.LVL763
	.4byte	0x4206
	.uleb128 0x31
	.4byte	.LVL765
	.4byte	0x671f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2c40
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x11
	.byte	0
	.uleb128 0x4
	.4byte	0x2c30
	.uleb128 0x3b
	.4byte	.LASF458
	.byte	0x2
	.2byte	0x7fc
	.4byte	0x64
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2cb0
	.uleb128 0x4e
	.ascii	"p\000"
	.byte	0x2
	.2byte	0x7fc
	.4byte	0x12c0
	.4byte	.LLST61
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x7fe
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x7fe
	.4byte	0x301
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7164
	.uleb128 0x31
	.4byte	.LVL193
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL195
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL196
	.4byte	0x4afe
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF459
	.byte	0x2
	.2byte	0x7ef
	.4byte	0x64
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2d01
	.uleb128 0x31
	.4byte	.LVL1265
	.4byte	0x2d27
	.uleb128 0x31
	.4byte	.LVL1266
	.4byte	0x2d01
	.uleb128 0x31
	.4byte	.LVL1267
	.4byte	0x2d27
	.uleb128 0x31
	.4byte	.LVL1268
	.4byte	0x2d01
	.uleb128 0x31
	.4byte	.LVL1269
	.4byte	0x1f6f
	.uleb128 0x31
	.4byte	.LVL1270
	.4byte	0x232e
	.byte	0
	.uleb128 0x56
	.4byte	.LASF495
	.byte	0x2
	.2byte	0x7dc
	.byte	0x1
	.4byte	0x2d27
	.uleb128 0x3d
	.4byte	.LASF422
	.byte	0x2
	.2byte	0x7dc
	.4byte	0x12c0
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x7de
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF460
	.byte	0x2
	.2byte	0x677
	.4byte	0x64
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3072
	.uleb128 0x44
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x677
	.4byte	0x12c0
	.4byte	.LLST196
	.uleb128 0x45
	.4byte	.LASF461
	.byte	0x2
	.2byte	0x679
	.4byte	0x301
	.4byte	.LLST197
	.uleb128 0x45
	.4byte	.LASF462
	.byte	0x2
	.2byte	0x679
	.4byte	0x301
	.4byte	.LLST198
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x67a
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF172
	.byte	0x2
	.2byte	0x67a
	.4byte	0x301
	.4byte	.LLST199
	.uleb128 0x3e
	.4byte	.LASF170
	.byte	0x2
	.2byte	0x67a
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF463
	.byte	0x2
	.2byte	0x67b
	.4byte	0x301
	.4byte	.LLST200
	.uleb128 0x45
	.4byte	.LASF464
	.byte	0x2
	.2byte	0x67b
	.4byte	0x301
	.4byte	.LLST201
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x67c
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF465
	.byte	0x2
	.2byte	0x67d
	.4byte	0x64
	.4byte	.LLST202
	.uleb128 0x3e
	.4byte	.LASF466
	.byte	0x2
	.2byte	0x67d
	.4byte	0x64
	.uleb128 0x3e
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x67e
	.4byte	0x301
	.uleb128 0x52
	.ascii	"n\000"
	.byte	0x2
	.2byte	0x67e
	.4byte	0x301
	.4byte	.LLST203
	.uleb128 0x3f
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x67e
	.4byte	0x301
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x67f
	.4byte	0x30c
	.4byte	.LLST204
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x67f
	.4byte	0x30c
	.4byte	.LLST205
	.uleb128 0x45
	.4byte	.LASF406
	.byte	0x2
	.2byte	0x67f
	.4byte	0x30c
	.4byte	.LLST206
	.uleb128 0x45
	.4byte	.LASF398
	.byte	0x2
	.2byte	0x67f
	.4byte	0x30c
	.4byte	.LLST207
	.uleb128 0x45
	.4byte	.LASF412
	.byte	0x2
	.2byte	0x680
	.4byte	0x301
	.4byte	.LLST208
	.uleb128 0x3e
	.4byte	.LASF467
	.byte	0x2
	.2byte	0x680
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF468
	.byte	0x2
	.2byte	0x681
	.4byte	0x30c
	.4byte	.LLST209
	.uleb128 0x3e
	.4byte	.LASF469
	.byte	0x2
	.2byte	0x682
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x683
	.4byte	0x1d3f
	.uleb128 0x3e
	.4byte	.LASF470
	.byte	0x2
	.2byte	0x684
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x3082
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7107
	.uleb128 0x48
	.4byte	.LASF472
	.byte	0x2
	.2byte	0x713
	.4byte	.L1135
	.uleb128 0x48
	.4byte	.LASF402
	.byte	0x2
	.2byte	0x71b
	.4byte	.L1147
	.uleb128 0x48
	.4byte	.LASF473
	.byte	0x2
	.2byte	0x7ce
	.4byte	.L1155
	.uleb128 0x40
	.4byte	.LASF474
	.byte	0x2
	.2byte	0x7d8
	.uleb128 0x53
	.4byte	.LBB449
	.4byte	.LBE449-.LBB449
	.4byte	0x2f01
	.uleb128 0x3e
	.4byte	.LASF475
	.byte	0x2
	.2byte	0x6eb
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF476
	.byte	0x2
	.2byte	0x6ec
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL954
	.4byte	0x1af8
	.byte	0
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x1d0
	.4byte	0x2f73
	.uleb128 0x45
	.4byte	.LASF477
	.byte	0x2
	.2byte	0x746
	.4byte	0x30c
	.4byte	.LLST210
	.uleb128 0x45
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x747
	.4byte	0x30c
	.4byte	.LLST211
	.uleb128 0x31
	.4byte	.LVL996
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL999
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1004
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1006
	.4byte	0x451e
	.uleb128 0x31
	.4byte	.LVL1008
	.4byte	0x3087
	.uleb128 0x31
	.4byte	.LVL1011
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1013
	.4byte	0x451e
	.uleb128 0x31
	.4byte	.LVL1014
	.4byte	0x451e
	.byte	0
	.uleb128 0x53
	.4byte	.LBB453
	.4byte	.LBE453-.LBB453
	.4byte	0x2fb4
	.uleb128 0x3e
	.4byte	.LASF479
	.byte	0x2
	.2byte	0x785
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x786
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL1019
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1020
	.4byte	0x451e
	.uleb128 0x31
	.4byte	.LVL1021
	.4byte	0x3929
	.byte	0
	.uleb128 0x31
	.4byte	.LVL915
	.4byte	0x4486
	.uleb128 0x31
	.4byte	.LVL917
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL922
	.4byte	0x4595
	.uleb128 0x31
	.4byte	.LVL930
	.4byte	0x313e
	.uleb128 0x31
	.4byte	.LVL935
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL937
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL941
	.4byte	0x451e
	.uleb128 0x31
	.4byte	.LVL957
	.4byte	0x369b
	.uleb128 0x31
	.4byte	.LVL962
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL971
	.4byte	0x4595
	.uleb128 0x31
	.4byte	.LVL981
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL986
	.4byte	0x451e
	.uleb128 0x31
	.4byte	.LVL991
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL994
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL995
	.4byte	0x451e
	.uleb128 0x31
	.4byte	.LVL1000
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1002
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1017
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1018
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1023
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1026
	.4byte	0x232e
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3082
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x15
	.byte	0
	.uleb128 0x4
	.4byte	0x3072
	.uleb128 0x50
	.4byte	.LASF480
	.byte	0x2
	.2byte	0x65b
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x313e
	.uleb128 0x4e
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x65b
	.4byte	0x30c
	.4byte	.LLST141
	.uleb128 0x44
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x65b
	.4byte	0x30c
	.4byte	.LLST142
	.uleb128 0x45
	.4byte	.LASF412
	.byte	0x2
	.2byte	0x65d
	.4byte	0x301
	.4byte	.LLST143
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7073
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x138
	.4byte	0x312b
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x661
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF392
	.byte	0x2
	.2byte	0x662
	.4byte	0x301
	.4byte	.LLST144
	.uleb128 0x45
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x663
	.4byte	0x301
	.4byte	.LLST145
	.uleb128 0x31
	.4byte	.LVL624
	.4byte	0x4087
	.uleb128 0x31
	.4byte	.LVL625
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL626
	.4byte	0x4206
	.byte	0
	.uleb128 0x31
	.4byte	.LVL619
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL631
	.4byte	0x3929
	.byte	0
	.uleb128 0x50
	.4byte	.LASF481
	.byte	0x2
	.2byte	0x647
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x318f
	.uleb128 0x57
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x647
	.4byte	0x12c0
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x44
	.4byte	.LASF170
	.byte	0x2
	.2byte	0x647
	.4byte	0x301
	.4byte	.LLST59
	.uleb128 0x44
	.4byte	.LASF172
	.byte	0x2
	.2byte	0x647
	.4byte	0x301
	.4byte	.LLST60
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x649
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF482
	.byte	0x2
	.2byte	0x63e
	.4byte	0x64
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31bc
	.uleb128 0x31
	.4byte	.LVL742
	.4byte	0x31bc
	.uleb128 0x31
	.4byte	.LVL743
	.4byte	0x3215
	.byte	0
	.uleb128 0x50
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x627
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31f1
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x629
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL740
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL741
	.4byte	0x17c6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF484
	.byte	0x2
	.2byte	0x617
	.4byte	0x64
	.4byte	.LFB226
	.4byte	.LFE226-.LFB226
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3215
	.uleb128 0x31
	.4byte	.LVL739
	.4byte	0x3215
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF485
	.byte	0x2
	.2byte	0x5b9
	.4byte	0x64
	.4byte	.LFB225
	.4byte	.LFE225-.LFB225
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3323
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x5b9
	.4byte	0x3323
	.4byte	.LLST157
	.uleb128 0x45
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x5bb
	.4byte	0x322
	.4byte	.LLST158
	.uleb128 0x3e
	.4byte	.LASF85
	.byte	0x2
	.2byte	0x5bb
	.4byte	0x322
	.uleb128 0x45
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x5bc
	.4byte	0x322
	.4byte	.LLST159
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x5bd
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x5be
	.4byte	0x539
	.4byte	.LLST160
	.uleb128 0x45
	.4byte	.LASF98
	.byte	0x2
	.2byte	0x5bf
	.4byte	0x630
	.4byte	.LLST161
	.uleb128 0x45
	.4byte	.LASF99
	.byte	0x2
	.2byte	0x5c0
	.4byte	0x539
	.4byte	.LLST162
	.uleb128 0x1f
	.4byte	.LASF95
	.byte	0x2
	.2byte	0x5c1
	.4byte	0x301
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x45
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x5c2
	.4byte	0x301
	.4byte	.LLST163
	.uleb128 0x3e
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x5c3
	.4byte	0x3329
	.uleb128 0x31
	.4byte	.LVL704
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL712
	.4byte	0x4486
	.uleb128 0x31
	.4byte	.LVL719
	.4byte	0x3dcf
	.uleb128 0x31
	.4byte	.LVL720
	.4byte	0x3d77
	.uleb128 0x31
	.4byte	.LVL721
	.4byte	0x3cac
	.uleb128 0x31
	.4byte	.LVL722
	.4byte	0x3cac
	.uleb128 0x31
	.4byte	.LVL724
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL730
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL733
	.4byte	0x1af8
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x587
	.uleb128 0x8
	.byte	0x4
	.4byte	0x7b8
	.uleb128 0x4d
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x54f
	.4byte	0x64
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x33de
	.uleb128 0x45
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x551
	.4byte	0x322
	.4byte	.LLST296
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7010
	.uleb128 0x31
	.4byte	.LVL1544
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1547
	.4byte	0x4486
	.uleb128 0x31
	.4byte	.LVL1549
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1550
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1551
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1552
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1553
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1556
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1557
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1558
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1559
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1560
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1562
	.4byte	0x1af8
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF493
	.byte	0x2
	.2byte	0x54a
	.4byte	0x64
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3409
	.uleb128 0x44
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x54a
	.4byte	0x301
	.4byte	.LLST58
	.byte	0
	.uleb128 0x56
	.4byte	.LASF496
	.byte	0x2
	.2byte	0x542
	.byte	0x1
	.4byte	0x342f
	.uleb128 0x3d
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x542
	.4byte	0x301
	.uleb128 0x3d
	.4byte	.LASF372
	.byte	0x2
	.2byte	0x542
	.4byte	0x64
	.byte	0
	.uleb128 0x43
	.4byte	.LASF498
	.byte	0x2
	.2byte	0x535
	.4byte	0x64
	.byte	0x1
	.uleb128 0x3b
	.4byte	.LASF499
	.byte	0x2
	.2byte	0x446
	.4byte	0x64
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3588
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x448
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF500
	.byte	0x2
	.2byte	0x448
	.4byte	0x301
	.4byte	.LLST154
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x449
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x449
	.4byte	0x301
	.4byte	.LLST155
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x44a
	.4byte	0x322
	.uleb128 0x3f
	.ascii	"j\000"
	.byte	0x2
	.2byte	0x44a
	.4byte	0x322
	.uleb128 0x45
	.4byte	.LASF501
	.byte	0x2
	.2byte	0x44b
	.4byte	0x3329
	.4byte	.LLST156
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x29c3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6953
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB395
	.4byte	.LBE395-.LBB395
	.byte	0x2
	.2byte	0x453
	.4byte	0x3512
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB396
	.4byte	.LBE396-.LBB396
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB397
	.4byte	.LBE397-.LBB397
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL671
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL672
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL673
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL674
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL678
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL679
	.4byte	0x4afe
	.uleb128 0x31
	.4byte	.LVL686
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL687
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL688
	.4byte	0x436e
	.uleb128 0x31
	.4byte	.LVL689
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL690
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL691
	.4byte	0x436e
	.uleb128 0x31
	.4byte	.LVL692
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF502
	.byte	0x2
	.2byte	0x3eb
	.4byte	0x64
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3649
	.uleb128 0x45
	.4byte	.LASF86
	.byte	0x2
	.2byte	0x3ed
	.4byte	0x301
	.4byte	.LLST232
	.uleb128 0x3e
	.4byte	.LASF503
	.byte	0x2
	.2byte	0x3ee
	.4byte	0x3649
	.uleb128 0x45
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x3ef
	.4byte	0x301
	.4byte	.LLST233
	.uleb128 0x40
	.4byte	.LASF505
	.byte	0x2
	.2byte	0x41c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6931
	.uleb128 0x31
	.4byte	.LVL1142
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1143
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1144
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1145
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1146
	.4byte	0x3709
	.uleb128 0x31
	.4byte	.LVL1147
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1148
	.4byte	0x4250
	.uleb128 0x31
	.4byte	.LVL1149
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1150
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1153
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1154
	.4byte	0x436e
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x810
	.uleb128 0x56
	.4byte	.LASF506
	.byte	0x2
	.2byte	0x3b2
	.byte	0x1
	.4byte	0x369b
	.uleb128 0x28
	.uleb128 0x3f
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x3b7
	.4byte	0x30c
	.uleb128 0x3f
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x3b8
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF507
	.byte	0x2
	.2byte	0x3b9
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x3ba
	.4byte	0x32d
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x3bb
	.4byte	0x1d3f
	.byte	0
	.byte	0
	.uleb128 0x50
	.4byte	.LASF508
	.byte	0x2
	.2byte	0x380
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3709
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x380
	.4byte	0x3323
	.4byte	.LLST151
	.uleb128 0x52
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x382
	.4byte	0x30c
	.4byte	.LLST152
	.uleb128 0x3e
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x383
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x384
	.4byte	0x539
	.4byte	.LLST153
	.uleb128 0x31
	.4byte	.LVL665
	.4byte	0x1af8
	.uleb128 0x24
	.4byte	.LVL667
	.4byte	0x3ac6
	.uleb128 0x31
	.4byte	.LVL669
	.4byte	0x17c6
	.byte	0
	.uleb128 0x56
	.4byte	.LASF509
	.byte	0x2
	.2byte	0x371
	.byte	0x1
	.4byte	0x372d
	.uleb128 0x3d
	.4byte	.LASF183
	.byte	0x2
	.2byte	0x371
	.4byte	0x30c
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x373
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF510
	.byte	0x2
	.2byte	0x35c
	.4byte	0x64
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x377a
	.uleb128 0x44
	.4byte	.LASF511
	.byte	0x2
	.2byte	0x35c
	.4byte	0x30c
	.4byte	.LLST224
	.uleb128 0x45
	.4byte	.LASF512
	.byte	0x2
	.2byte	0x35e
	.4byte	0x30c
	.4byte	.LLST225
	.uleb128 0x31
	.4byte	.LVL1117
	.4byte	0x3850
	.uleb128 0x31
	.4byte	.LVL1118
	.4byte	0x2b73
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x336
	.4byte	0x30c
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x384a
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x336
	.4byte	0x30c
	.4byte	.LLST226
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x336
	.4byte	0x30c
	.4byte	.LLST227
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x336
	.4byte	0x384a
	.4byte	.LLST228
	.uleb128 0x52
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x338
	.4byte	0x301
	.4byte	.LLST229
	.uleb128 0x3e
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x338
	.4byte	0x301
	.uleb128 0x52
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x339
	.4byte	0x30c
	.4byte	.LLST230
	.uleb128 0x1f
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x33a
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x51
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x33b
	.4byte	0x32d
	.uleb128 0x3
	.byte	0x91
	.sleb128 -124
	.uleb128 0x45
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x33c
	.4byte	0x30c
	.4byte	.LLST231
	.uleb128 0x1f
	.4byte	.LASF353
	.byte	0x2
	.2byte	0x33d
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -104
	.uleb128 0x31
	.4byte	.LVL1130
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1131
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1133
	.4byte	0x17c6
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x2f6
	.uleb128 0x4d
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x311
	.4byte	0x30c
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3929
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x311
	.4byte	0x30c
	.4byte	.LLST218
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x311
	.4byte	0x30c
	.4byte	.LLST219
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x311
	.4byte	0x384a
	.4byte	.LLST220
	.uleb128 0x52
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x313
	.4byte	0x301
	.4byte	.LLST221
	.uleb128 0x3e
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x313
	.4byte	0x301
	.uleb128 0x52
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x314
	.4byte	0x30c
	.4byte	.LLST222
	.uleb128 0x1f
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x315
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x51
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x316
	.4byte	0x32d
	.uleb128 0x3
	.byte	0x91
	.sleb128 -124
	.uleb128 0x45
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x317
	.4byte	0x30c
	.4byte	.LLST223
	.uleb128 0x1f
	.4byte	.LASF353
	.byte	0x2
	.2byte	0x318
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -104
	.uleb128 0x31
	.4byte	.LVL1100
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1101
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1102
	.4byte	0x3ac6
	.uleb128 0x31
	.4byte	.LVL1105
	.4byte	0x17c6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF521
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x64
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x39df
	.uleb128 0x4e
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x30c
	.4byte	.LLST137
	.uleb128 0x4e
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x539
	.4byte	.LLST138
	.uleb128 0x44
	.4byte	.LASF522
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x64
	.4byte	.LLST139
	.uleb128 0x45
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x2f2
	.4byte	0x301
	.4byte	.LLST140
	.uleb128 0x1f
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x2f2
	.4byte	0x301
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x1f
	.4byte	.LASF525
	.byte	0x2
	.2byte	0x2f2
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x39ef
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6858
	.uleb128 0x48
	.4byte	.LASF526
	.byte	0x2
	.2byte	0x2fb
	.4byte	.L739
	.uleb128 0x31
	.4byte	.LVL606
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL608
	.4byte	0x3a2f
	.uleb128 0x31
	.4byte	.LVL609
	.4byte	0x3a86
	.uleb128 0x31
	.4byte	.LVL610
	.4byte	0x3e80
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x39ef
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x8
	.byte	0
	.uleb128 0x4
	.4byte	0x39df
	.uleb128 0x3b
	.4byte	.LASF527
	.byte	0x2
	.2byte	0x2e3
	.4byte	0x64
	.4byte	.LFB211
	.4byte	.LFE211-.LFB211
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a2f
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x2e5
	.4byte	0x301
	.4byte	.LLST195
	.uleb128 0x31
	.4byte	.LVL902
	.4byte	0x364f
	.uleb128 0x31
	.4byte	.LVL905
	.4byte	0x3a86
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF528
	.byte	0x2
	.2byte	0x2bb
	.4byte	0x301
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a86
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x2bd
	.4byte	0x301
	.uleb128 0x1f
	.4byte	.LASF529
	.byte	0x2
	.2byte	0x2be
	.4byte	0x301
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x3e
	.4byte	.LASF530
	.byte	0x2
	.2byte	0x2bf
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x3082
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6841
	.uleb128 0x31
	.4byte	.LVL174
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF531
	.byte	0x2
	.2byte	0x2b1
	.4byte	0x64
	.4byte	.LFB209
	.4byte	.LFE209-.LFB209
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ac6
	.uleb128 0x44
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x2b1
	.4byte	0x301
	.4byte	.LLST136
	.uleb128 0x3e
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x2b3
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL601
	.4byte	0x3ac6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF532
	.byte	0x2
	.2byte	0x276
	.4byte	0x64
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3bdc
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x276
	.4byte	0x3323
	.4byte	.LLST129
	.uleb128 0x44
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x276
	.4byte	0x30c
	.4byte	.LLST130
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x276
	.4byte	0x539
	.4byte	.LLST131
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x278
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x279
	.4byte	0x3329
	.4byte	.LLST132
	.uleb128 0x45
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x27a
	.4byte	0x301
	.4byte	.LLST133
	.uleb128 0x40
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x27c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6816
	.uleb128 0x4f
	.4byte	0x17c6
	.4byte	.LBB361
	.4byte	.Ldebug_ranges0+0x120
	.byte	0x2
	.2byte	0x292
	.4byte	0x3ba5
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x120
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x120
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL575
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL572
	.4byte	0x3bdc
	.uleb128 0x31
	.4byte	.LVL573
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL574
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL578
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL581
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL582
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF534
	.byte	0x2
	.2byte	0x246
	.4byte	0x64
	.4byte	.LFB207
	.4byte	.LFE207-.LFB207
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3c97
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x246
	.4byte	0x3323
	.4byte	.LLST125
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x248
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x248
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF535
	.byte	0x2
	.2byte	0x249
	.4byte	0x301
	.4byte	.LLST126
	.uleb128 0x45
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x24a
	.4byte	0x630
	.4byte	.LLST127
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x24b
	.4byte	0x539
	.4byte	.LLST128
	.uleb128 0x3e
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x24c
	.4byte	0x3329
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x3ca7
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6802
	.uleb128 0x31
	.4byte	.LVL555
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL556
	.4byte	0x4250
	.uleb128 0x31
	.4byte	.LVL562
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL565
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL566
	.4byte	0x3cac
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3ca7
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1e
	.byte	0
	.uleb128 0x4
	.4byte	0x3c97
	.uleb128 0x3b
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x218
	.4byte	0x64
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3d77
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x218
	.4byte	0x3323
	.4byte	.LLST134
	.uleb128 0x1f
	.4byte	.LASF538
	.byte	0x2
	.2byte	0x21a
	.4byte	0x301
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x3e
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x21b
	.4byte	0x301
	.uleb128 0x1f
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x21c
	.4byte	0x301
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x45
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x21d
	.4byte	0x630
	.4byte	.LLST135
	.uleb128 0x1f
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x21e
	.4byte	0x539
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x3e
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x21f
	.4byte	0x3329
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6788
	.uleb128 0x31
	.4byte	.LVL588
	.4byte	0x3dcf
	.uleb128 0x31
	.4byte	.LVL590
	.4byte	0x3d77
	.uleb128 0x31
	.4byte	.LVL592
	.4byte	0x436e
	.uleb128 0x31
	.4byte	.LVL594
	.4byte	0x3d77
	.uleb128 0x31
	.4byte	.LVL596
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL597
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL598
	.4byte	0x3ac6
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF540
	.byte	0x2
	.2byte	0x203
	.4byte	0x64
	.byte	0x1
	.4byte	0x3dba
	.uleb128 0x3d
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x203
	.4byte	0x3323
	.uleb128 0x3e
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x205
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x206
	.4byte	0x630
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x3dca
	.4byte	.LASF540
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3dca
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x19
	.byte	0
	.uleb128 0x4
	.4byte	0x3dba
	.uleb128 0x3b
	.4byte	.LASF542
	.byte	0x2
	.2byte	0x1db
	.4byte	0x64
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3e80
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x1db
	.4byte	0x3323
	.4byte	.LLST120
	.uleb128 0x1f
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x1dd
	.4byte	0x630
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x1de
	.4byte	0x539
	.4byte	.LLST121
	.uleb128 0x1f
	.4byte	.LASF543
	.byte	0x2
	.2byte	0x1df
	.4byte	0x630
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x45
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x1e0
	.4byte	0x301
	.4byte	.LLST122
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x1e0
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x1e1
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF544
	.byte	0x2
	.2byte	0x1e2
	.4byte	0x301
	.4byte	.LLST123
	.uleb128 0x45
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x1e3
	.4byte	0x301
	.4byte	.LLST124
	.uleb128 0x31
	.4byte	.LVL543
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL552
	.4byte	0x436e
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF545
	.byte	0x2
	.2byte	0x1b9
	.4byte	0x64
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3f27
	.uleb128 0x44
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x1b9
	.4byte	0x301
	.4byte	.LLST117
	.uleb128 0x44
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x1b9
	.4byte	0x301
	.4byte	.LLST118
	.uleb128 0x45
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x1bb
	.4byte	0x3329
	.4byte	.LLST119
	.uleb128 0x1f
	.4byte	.LASF546
	.byte	0x2
	.2byte	0x1bc
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6745
	.uleb128 0x31
	.4byte	.LVL526
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL528
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL531
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL533
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL534
	.4byte	0x6735
	.uleb128 0x31
	.4byte	.LVL535
	.4byte	0x6735
	.uleb128 0x31
	.4byte	.LVL536
	.4byte	0x671f
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF547
	.byte	0x2
	.2byte	0x193
	.4byte	0x64
	.byte	0x1
	.4byte	0x3fa6
	.uleb128 0x3d
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x193
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF548
	.byte	0x2
	.2byte	0x195
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF549
	.byte	0x2
	.2byte	0x196
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF550
	.byte	0x2
	.2byte	0x197
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF551
	.byte	0x2
	.2byte	0x199
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF552
	.byte	0x2
	.2byte	0x19a
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x19b
	.4byte	0x1210
	.uleb128 0x3e
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x19b
	.4byte	0x1210
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x3082
	.4byte	.LASF547
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF554
	.byte	0x2
	.2byte	0x17f
	.4byte	0x64
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3fed
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x17f
	.4byte	0x301
	.4byte	.LLST47
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x181
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x182
	.4byte	0x1210
	.4byte	.LLST48
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF555
	.byte	0x2
	.2byte	0x17a
	.4byte	0x64
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4021
	.uleb128 0x44
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x17a
	.4byte	0x4021
	.4byte	.LLST46
	.uleb128 0x24
	.4byte	.LVL148
	.4byte	0x4027
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x1210
	.uleb128 0x3b
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x167
	.4byte	0x64
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4087
	.uleb128 0x44
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x167
	.4byte	0x4021
	.4byte	.LLST43
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x167
	.4byte	0x301
	.4byte	.LLST44
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x169
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x16a
	.4byte	0x1210
	.4byte	.LLST45
	.uleb128 0x31
	.4byte	.LVL142
	.4byte	0x4087
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF558
	.byte	0x2
	.2byte	0x14c
	.4byte	0x64
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x40f4
	.uleb128 0x44
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x14c
	.4byte	0x4021
	.4byte	.LLST41
	.uleb128 0x44
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x14c
	.4byte	0x301
	.4byte	.LLST42
	.uleb128 0x1f
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x14e
	.4byte	0x1210
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x3e
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x14e
	.4byte	0x1210
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x4104
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6704
	.uleb128 0x31
	.4byte	.LVL134
	.4byte	0x671f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x4104
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x10
	.byte	0
	.uleb128 0x4
	.4byte	0x40f4
	.uleb128 0x3b
	.4byte	.LASF560
	.byte	0x2
	.2byte	0x11b
	.4byte	0x64
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4178
	.uleb128 0x44
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x11b
	.4byte	0x301
	.4byte	.LLST38
	.uleb128 0x1f
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x11d
	.4byte	0x1210
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x1f
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x11d
	.4byte	0x1210
	.uleb128 0x1
	.byte	0x53
	.uleb128 0x45
	.4byte	.LASF561
	.byte	0x2
	.2byte	0x11e
	.4byte	0x301
	.4byte	.LLST39
	.uleb128 0x3e
	.4byte	.LASF562
	.byte	0x2
	.2byte	0x11f
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF563
	.byte	0x2
	.2byte	0x120
	.4byte	0x301
	.byte	0
	.uleb128 0x22
	.4byte	.LASF564
	.byte	0x2
	.byte	0xd8
	.4byte	0x64
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4206
	.uleb128 0x23
	.4byte	.LASF432
	.byte	0x2
	.byte	0xd8
	.4byte	0x301
	.4byte	.LLST30
	.uleb128 0x2c
	.4byte	.LASF435
	.byte	0x2
	.byte	0xda
	.4byte	0x1210
	.4byte	.LLST31
	.uleb128 0x2c
	.4byte	.LASF559
	.byte	0x2
	.byte	0xda
	.4byte	0x1210
	.4byte	.LLST32
	.uleb128 0x2c
	.4byte	.LASF565
	.byte	0x2
	.byte	0xdb
	.4byte	0x301
	.4byte	.LLST33
	.uleb128 0x2c
	.4byte	.LASF566
	.byte	0x2
	.byte	0xdc
	.4byte	0x301
	.4byte	.LLST34
	.uleb128 0x2c
	.4byte	.LASF550
	.byte	0x2
	.byte	0xdd
	.4byte	0x30c
	.4byte	.LLST35
	.uleb128 0x2c
	.4byte	.LASF567
	.byte	0x2
	.byte	0xde
	.4byte	0x30c
	.4byte	.LLST36
	.uleb128 0x35
	.4byte	.LASF563
	.byte	0x2
	.byte	0xdf
	.4byte	0x301
	.byte	0
	.uleb128 0x58
	.4byte	.LASF568
	.byte	0x2
	.byte	0xb9
	.byte	0x1
	.4byte	0x422b
	.uleb128 0x34
	.4byte	.LASF432
	.byte	0x2
	.byte	0xb9
	.4byte	0x301
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x4104
	.4byte	.LASF568
	.byte	0
	.uleb128 0x58
	.4byte	.LASF569
	.byte	0x2
	.byte	0xb2
	.byte	0x1
	.4byte	0x4250
	.uleb128 0x34
	.4byte	.LASF432
	.byte	0x2
	.byte	0xb2
	.4byte	0x301
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x4104
	.4byte	.LASF569
	.byte	0
	.uleb128 0x22
	.4byte	.LASF570
	.byte	0x2
	.byte	0xa4
	.4byte	0x301
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x429d
	.uleb128 0x37
	.ascii	"tmp\000"
	.byte	0x2
	.byte	0xa6
	.4byte	0x301
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x53
	.4byte	.LBB253
	.4byte	.LBE253-.LBB253
	.4byte	0x428d
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0xa9
	.4byte	0x429d
	.byte	0
	.uleb128 0x59
	.4byte	0x43fb
	.4byte	.LBB251
	.4byte	.LBE251-.LBB251
	.byte	0x2
	.byte	0xa8
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0xadc
	.uleb128 0x22
	.4byte	.LASF571
	.byte	0x2
	.byte	0x92
	.4byte	0x64
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x42fc
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x92
	.4byte	0x301
	.4byte	.LLST29
	.uleb128 0x37
	.ascii	"ret\000"
	.byte	0x2
	.byte	0x94
	.4byte	0x64
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x37
	.ascii	"i\000"
	.byte	0x2
	.byte	0x95
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x96
	.4byte	0x429d
	.uleb128 0x5a
	.4byte	0x43ef
	.4byte	.LBB247
	.4byte	.Ldebug_ranges0+0x50
	.byte	0x2
	.byte	0x98
	.byte	0
	.uleb128 0x49
	.4byte	.LASF572
	.byte	0x2
	.byte	0x73
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x436e
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x2
	.byte	0x75
	.4byte	0x301
	.uleb128 0x4a
	.ascii	"tmp\000"
	.byte	0x2
	.byte	0x75
	.4byte	0x301
	.4byte	.LLST26
	.uleb128 0x29
	.ascii	"j\000"
	.byte	0x2
	.byte	0x75
	.4byte	0x301
	.uleb128 0x2c
	.4byte	.LASF383
	.byte	0x2
	.byte	0x76
	.4byte	0x301
	.4byte	.LLST27
	.uleb128 0x35
	.4byte	.LASF573
	.byte	0x2
	.byte	0x77
	.4byte	0x301
	.uleb128 0x2c
	.4byte	.LASF574
	.byte	0x2
	.byte	0x78
	.4byte	0x30c
	.4byte	.LLST28
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x79
	.4byte	0x429d
	.uleb128 0x31
	.4byte	.LVL80
	.4byte	0x4561
	.byte	0
	.uleb128 0x49
	.4byte	.LASF575
	.byte	0x2
	.byte	0x61
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x43ef
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x61
	.4byte	0x301
	.4byte	.LLST98
	.uleb128 0x23
	.4byte	.LASF576
	.byte	0x2
	.byte	0x61
	.4byte	0x301
	.4byte	.LLST99
	.uleb128 0x53
	.4byte	.LBB299
	.4byte	.LBE299-.LBB299
	.4byte	0x43df
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x64
	.4byte	0x429d
	.uleb128 0x33
	.4byte	.LBB300
	.4byte	.LBE300-.LBB300
	.uleb128 0x35
	.4byte	.LASF577
	.byte	0x2
	.byte	0x66
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL411
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL412
	.4byte	0x1954
	.byte	0
	.byte	0
	.uleb128 0x59
	.4byte	0x43ef
	.4byte	.LBB297
	.4byte	.LBE297-.LBB297
	.byte	0x2
	.byte	0x63
	.byte	0
	.uleb128 0x5b
	.4byte	.LASF578
	.byte	0x2
	.byte	0x5c
	.4byte	0x301
	.byte	0x1
	.uleb128 0x5b
	.4byte	.LASF579
	.byte	0x2
	.byte	0x57
	.4byte	0x301
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF580
	.byte	0x2
	.byte	0x4b
	.4byte	0x301
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4486
	.uleb128 0x2b
	.ascii	"max\000"
	.byte	0x2
	.byte	0x4b
	.4byte	0x301
	.4byte	.LLST25
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x4d
	.4byte	0x429d
	.uleb128 0x5c
	.4byte	0x17c6
	.4byte	.LBB238
	.4byte	.Ldebug_ranges0+0x30
	.byte	0x2
	.byte	0x53
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x30
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x30
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL76
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x22
	.4byte	.LASF581
	.byte	0x2
	.byte	0x2e
	.4byte	0x64
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x451e
	.uleb128 0x23
	.4byte	.LASF178
	.byte	0x2
	.byte	0x2e
	.4byte	0x30c
	.4byte	.LLST88
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x2
	.byte	0x2e
	.4byte	0x64
	.4byte	.LLST89
	.uleb128 0x37
	.ascii	"req\000"
	.byte	0x2
	.byte	0x30
	.4byte	0x32d
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x37
	.ascii	"low\000"
	.byte	0x2
	.byte	0x31
	.4byte	0x322
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x20
	.4byte	.LASF582
	.byte	0x2
	.byte	0x31
	.4byte	0x322
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"mid\000"
	.byte	0x2
	.byte	0x31
	.4byte	0x322
	.uleb128 0x20
	.4byte	.LASF583
	.byte	0x2
	.byte	0x32
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -88
	.uleb128 0x35
	.4byte	.LASF584
	.byte	0x2
	.byte	0x33
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL374
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL377
	.4byte	0x1af8
	.byte	0
	.uleb128 0x22
	.4byte	.LASF585
	.byte	0x2
	.byte	0x1e
	.4byte	0x30c
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4561
	.uleb128 0x23
	.4byte	.LASF586
	.byte	0x2
	.byte	0x1e
	.4byte	0x30c
	.4byte	.LLST24
	.uleb128 0x4c
	.4byte	.LASF587
	.byte	0x2
	.byte	0x1e
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x37
	.ascii	"ret\000"
	.byte	0x2
	.byte	0x20
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF588
	.byte	0x2
	.byte	0x14
	.4byte	0x301
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4595
	.uleb128 0x23
	.4byte	.LASF378
	.byte	0x2
	.byte	0x14
	.4byte	0x301
	.4byte	.LLST23
	.uleb128 0x35
	.4byte	.LASF500
	.byte	0x2
	.byte	0x16
	.4byte	0x301
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF589
	.byte	0x2
	.byte	0xc
	.4byte	0x301
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x45c9
	.uleb128 0x23
	.4byte	.LASF378
	.byte	0x2
	.byte	0xc
	.4byte	0x301
	.4byte	.LLST22
	.uleb128 0x35
	.4byte	.LASF376
	.byte	0x2
	.byte	0xe
	.4byte	0x301
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF590
	.byte	0x2
	.byte	0x4
	.4byte	0x301
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x460c
	.uleb128 0x23
	.4byte	.LASF376
	.byte	0x2
	.byte	0x4
	.4byte	0x301
	.4byte	.LLST20
	.uleb128 0x23
	.4byte	.LASF500
	.byte	0x2
	.byte	0x4
	.4byte	0x301
	.4byte	.LLST21
	.uleb128 0x35
	.4byte	.LASF392
	.byte	0x2
	.byte	0x6
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF591
	.byte	0x4
	.2byte	0x13c
	.4byte	0x317
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x47d0
	.uleb128 0x52
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x13e
	.4byte	0x30c
	.4byte	.LLST216
	.uleb128 0x3f
	.ascii	"blk\000"
	.byte	0x4
	.2byte	0x13f
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF352
	.byte	0x4
	.2byte	0x140
	.4byte	0x30c
	.4byte	.LLST217
	.uleb128 0x3e
	.4byte	.LASF592
	.byte	0x4
	.2byte	0x142
	.4byte	0x47d0
	.uleb128 0x40
	.4byte	.LASF593
	.byte	0x4
	.2byte	0x158
	.uleb128 0x4f
	.4byte	0x17c6
	.4byte	.LBB464
	.4byte	.Ldebug_ranges0+0x1f0
	.byte	0x4
	.2byte	0x15e
	.4byte	0x46b8
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1f0
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1f0
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL1074
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB470
	.4byte	.LBE470-.LBB470
	.byte	0x4
	.2byte	0x15f
	.4byte	0x4712
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB471
	.4byte	.LBE471-.LBB471
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB472
	.4byte	.LBE472-.LBB472
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL1075
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1061
	.4byte	0x4938
	.uleb128 0x31
	.4byte	.LVL1062
	.4byte	0x47d6
	.uleb128 0x31
	.4byte	.LVL1063
	.4byte	0x4afe
	.uleb128 0x31
	.4byte	.LVL1064
	.4byte	0x1900
	.uleb128 0x31
	.4byte	.LVL1065
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1068
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1069
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1070
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1071
	.4byte	0x180a
	.uleb128 0x31
	.4byte	.LVL1072
	.4byte	0x4a98
	.uleb128 0x31
	.4byte	.LVL1073
	.4byte	0x4afe
	.uleb128 0x31
	.4byte	.LVL1078
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1079
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL1080
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1081
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1082
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1083
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL1084
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1085
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL1086
	.4byte	0x499b
	.uleb128 0x31
	.4byte	.LVL1087
	.4byte	0x499b
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x700
	.uleb128 0x3b
	.4byte	.LASF594
	.byte	0x4
	.2byte	0x116
	.4byte	0x64
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4834
	.uleb128 0x51
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x118
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x52
	.ascii	"blk\000"
	.byte	0x4
	.2byte	0x119
	.4byte	0x301
	.4byte	.LLST86
	.uleb128 0x45
	.4byte	.LASF595
	.byte	0x4
	.2byte	0x11a
	.4byte	0x30c
	.4byte	.LLST87
	.uleb128 0x3e
	.4byte	.LASF592
	.byte	0x4
	.2byte	0x11b
	.4byte	0x47d0
	.uleb128 0x31
	.4byte	.LVL369
	.4byte	0x1af8
	.byte	0
	.uleb128 0x22
	.4byte	.LASF596
	.byte	0x4
	.byte	0xac
	.4byte	0x64
	.4byte	.LFB179
	.4byte	.LFE179-.LFB179
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x48e4
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x4
	.byte	0xae
	.4byte	0x30c
	.4byte	.LLST213
	.uleb128 0x4a
	.ascii	"blk\000"
	.byte	0x4
	.byte	0xaf
	.4byte	0x301
	.4byte	.LLST214
	.uleb128 0x2c
	.4byte	.LASF488
	.byte	0x4
	.byte	0xb0
	.4byte	0x322
	.4byte	.LLST215
	.uleb128 0x35
	.4byte	.LASF592
	.byte	0x4
	.byte	0xb1
	.4byte	0x47d0
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x48f4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6546
	.uleb128 0x31
	.4byte	.LVL1039
	.4byte	0x4938
	.uleb128 0x31
	.4byte	.LVL1041
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1042
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1043
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1044
	.4byte	0x4486
	.uleb128 0x31
	.4byte	.LVL1046
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1047
	.4byte	0x58b9
	.uleb128 0x31
	.4byte	.LVL1053
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1057
	.4byte	0x176f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x48f4
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xa
	.byte	0
	.uleb128 0x4
	.4byte	0x48e4
	.uleb128 0x22
	.4byte	.LASF597
	.byte	0x4
	.byte	0x9c
	.4byte	0x301
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4938
	.uleb128 0x4a
	.ascii	"blk\000"
	.byte	0x4
	.byte	0x9e
	.4byte	0x301
	.4byte	.LLST19
	.uleb128 0x20
	.4byte	.LASF598
	.byte	0x4
	.byte	0x9f
	.4byte	0x301
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x31
	.4byte	.LVL56
	.4byte	0x4afe
	.byte	0
	.uleb128 0x49
	.4byte	.LASF599
	.byte	0x4
	.byte	0x94
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x499b
	.uleb128 0x5c
	.4byte	0x17c6
	.4byte	.LBB227
	.4byte	.Ldebug_ranges0+0x18
	.byte	0x4
	.byte	0x98
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x18
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x18
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x24
	.4byte	.LVL53
	.4byte	0x6714
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x22
	.4byte	.LASF600
	.byte	0x4
	.byte	0x4f
	.4byte	0x64
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4a98
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x4
	.byte	0x51
	.4byte	0x64
	.4byte	.LLST179
	.uleb128 0x2c
	.4byte	.LASF601
	.byte	0x4
	.byte	0x52
	.4byte	0x64
	.4byte	.LLST180
	.uleb128 0x2c
	.4byte	.LASF592
	.byte	0x4
	.byte	0x53
	.4byte	0x47d0
	.4byte	.LLST181
	.uleb128 0x2c
	.4byte	.LASF504
	.byte	0x4
	.byte	0x54
	.4byte	0x301
	.4byte	.LLST182
	.uleb128 0x47
	.4byte	.LASF602
	.byte	0x4
	.byte	0x67
	.uleb128 0x5d
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB419
	.4byte	.Ldebug_ranges0+0x168
	.byte	0x4
	.byte	0x5f
	.4byte	0x4a4f
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x168
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x168
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL818
	.4byte	0x6714
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL815
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL820
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL821
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL822
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL823
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL826
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL827
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL829
	.4byte	0x176f
	.byte	0
	.uleb128 0x49
	.4byte	.LASF603
	.byte	0x4
	.byte	0x41
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4af6
	.uleb128 0x23
	.4byte	.LASF536
	.byte	0x4
	.byte	0x41
	.4byte	0x630
	.4byte	.LLST115
	.uleb128 0x23
	.4byte	.LASF604
	.byte	0x4
	.byte	0x41
	.4byte	0x539
	.4byte	.LLST116
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x43
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x29c3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6503
	.uleb128 0x31
	.4byte	.LVL522
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL523
	.4byte	0x671f
	.byte	0
	.uleb128 0x5e
	.4byte	.LASF605
	.byte	0x4
	.byte	0x26
	.byte	0x1
	.uleb128 0x2a
	.4byte	.LASF606
	.byte	0x4
	.byte	0x17
	.4byte	0x64
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4b59
	.uleb128 0x23
	.4byte	.LASF178
	.byte	0x4
	.byte	0x17
	.4byte	0x301
	.4byte	.LLST16
	.uleb128 0x4a
	.ascii	"die\000"
	.byte	0x4
	.byte	0x19
	.4byte	0x30c
	.4byte	.LLST17
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x1a
	.4byte	0x30c
	.uleb128 0x35
	.4byte	.LASF604
	.byte	0x4
	.byte	0x1b
	.4byte	0x539
	.uleb128 0x2c
	.4byte	.LASF369
	.byte	0x4
	.byte	0x1c
	.4byte	0x301
	.4byte	.LLST18
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF607
	.byte	0x4
	.byte	0x4
	.4byte	0x64
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4bb0
	.uleb128 0x23
	.4byte	.LASF178
	.byte	0x4
	.byte	0x4
	.4byte	0x301
	.4byte	.LLST14
	.uleb128 0x4a
	.ascii	"die\000"
	.byte	0x4
	.byte	0x6
	.4byte	0x30c
	.4byte	.LLST15
	.uleb128 0x35
	.4byte	.LASF604
	.byte	0x4
	.byte	0x7
	.4byte	0x539
	.uleb128 0x35
	.4byte	.LASF369
	.byte	0x4
	.byte	0x8
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL43
	.4byte	0x671f
	.byte	0
	.uleb128 0x5f
	.4byte	.LASF669
	.byte	0x3
	.2byte	0x4bb
	.4byte	0x30c
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x3b
	.4byte	.LASF608
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x64
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c1a
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x99
	.4byte	.LLST293
	.uleb128 0x44
	.4byte	.LASF114
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x99
	.4byte	.LLST294
	.uleb128 0x4e
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x4c1a
	.4byte	.LLST295
	.uleb128 0x24
	.4byte	.LVL1543
	.4byte	0x4d46
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x79
	.uleb128 0x3b
	.4byte	.LASF609
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x64
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c74
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x99
	.4byte	.LLST279
	.uleb128 0x44
	.4byte	.LASF114
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x99
	.4byte	.LLST280
	.uleb128 0x4e
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x4c1a
	.4byte	.LLST281
	.uleb128 0x24
	.4byte	.LVL1442
	.4byte	0x50c3
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF610
	.byte	0x3
	.2byte	0x481
	.4byte	0x64
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4d46
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x481
	.4byte	0x30c
	.4byte	.LLST244
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x481
	.4byte	0x30c
	.4byte	.LLST245
	.uleb128 0x45
	.4byte	.LASF517
	.byte	0x3
	.2byte	0x483
	.4byte	0x301
	.4byte	.LLST246
	.uleb128 0x3f
	.ascii	"tmp\000"
	.byte	0x3
	.2byte	0x484
	.4byte	0x301
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x485
	.4byte	0x30c
	.4byte	.LLST247
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x486
	.4byte	0x30c
	.4byte	.LLST248
	.uleb128 0x45
	.4byte	.LASF423
	.byte	0x3
	.2byte	0x486
	.4byte	0x30c
	.4byte	.LLST249
	.uleb128 0x3e
	.4byte	.LASF412
	.byte	0x3
	.2byte	0x487
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL1215
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1221
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1222
	.4byte	0x3588
	.uleb128 0x31
	.4byte	.LVL1223
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1226
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1227
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1228
	.4byte	0x232e
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x3be
	.4byte	0x64
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f66
	.uleb128 0x4e
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x3be
	.4byte	0x2f6
	.4byte	.LLST282
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x3be
	.4byte	0x30c
	.4byte	.LLST283
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x3be
	.4byte	0x30c
	.4byte	.LLST284
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x3be
	.4byte	0x384a
	.4byte	.LLST285
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x3
	.2byte	0x3c0
	.4byte	0x1d3f
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30c
	.4byte	.LLST286
	.uleb128 0x45
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30c
	.4byte	.LLST287
	.uleb128 0x45
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30c
	.4byte	.LLST288
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x3c2
	.4byte	0x30c
	.4byte	.LLST289
	.uleb128 0x3f
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x3c2
	.4byte	0x30c
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x30c
	.4byte	.LLST290
	.uleb128 0x45
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x30c
	.4byte	.LLST291
	.uleb128 0x3e
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF615
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF616
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF422
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x12c0
	.4byte	.LLST292
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x39ef
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6439
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x250
	.4byte	0x4e8d
	.uleb128 0x1f
	.4byte	.LASF385
	.byte	0x3
	.2byte	0x43a
	.4byte	0x32d
	.uleb128 0x2
	.byte	0x91
	.sleb128 -60
	.uleb128 0x31
	.4byte	.LVL1520
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1524
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1525
	.4byte	0x671f
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1446
	.4byte	0x3850
	.uleb128 0x31
	.4byte	.LVL1457
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1463
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1465
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1466
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1468
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1470
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1473
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1477
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1479
	.4byte	0x3fa6
	.uleb128 0x31
	.4byte	.LVL1480
	.4byte	0x1e6d
	.uleb128 0x31
	.4byte	.LVL1481
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1482
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1487
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1492
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1496
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1499
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1500
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL1501
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1514
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1521
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1526
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1534
	.4byte	0x4f8a
	.uleb128 0x31
	.4byte	.LVL1536
	.4byte	0x671f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF617
	.byte	0x3
	.2byte	0x3b5
	.4byte	0x64
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f8a
	.uleb128 0x31
	.4byte	.LVL1205
	.4byte	0x4f8a
	.byte	0
	.uleb128 0x50
	.4byte	.LASF618
	.byte	0x3
	.2byte	0x385
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x50ae
	.uleb128 0x4e
	.ascii	"req\000"
	.byte	0x3
	.2byte	0x385
	.4byte	0xf40
	.4byte	.LLST237
	.uleb128 0x44
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x385
	.4byte	0x30c
	.4byte	.LLST238
	.uleb128 0x44
	.4byte	.LASF619
	.byte	0x3
	.2byte	0x385
	.4byte	0x30c
	.4byte	.LLST239
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x3
	.2byte	0x385
	.4byte	0x12c0
	.4byte	.LLST240
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x387
	.4byte	0x30c
	.4byte	.LLST241
	.uleb128 0x52
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x388
	.4byte	0x30c
	.4byte	.LLST242
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x388
	.4byte	0x30c
	.4byte	.LLST243
	.uleb128 0x3e
	.4byte	.LASF405
	.byte	0x3
	.2byte	0x389
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x3
	.2byte	0x38a
	.4byte	0x1d3f
	.uleb128 0x3e
	.4byte	.LASF470
	.byte	0x3
	.2byte	0x38b
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x50be
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6411
	.uleb128 0x31
	.4byte	.LVL1184
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1186
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1187
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1188
	.4byte	0x1e11
	.uleb128 0x31
	.4byte	.LVL1189
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL1190
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1192
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1194
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1196
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1199
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1200
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1203
	.4byte	0x671f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x50be
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xc
	.byte	0
	.uleb128 0x4
	.4byte	0x50ae
	.uleb128 0x4d
	.4byte	.LASF620
	.byte	0x3
	.2byte	0x32c
	.4byte	0x64
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52a2
	.uleb128 0x4e
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x32c
	.4byte	0x2f6
	.4byte	.LLST264
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x32c
	.4byte	0x30c
	.4byte	.LLST265
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x32c
	.4byte	0x30c
	.4byte	.LLST266
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x32c
	.4byte	0x384a
	.4byte	.LLST267
	.uleb128 0x51
	.ascii	"ret\000"
	.byte	0x3
	.2byte	0x32e
	.4byte	0x64
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x45
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x32f
	.4byte	0x30c
	.4byte	.LLST268
	.uleb128 0x45
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x32f
	.4byte	0x30c
	.4byte	.LLST269
	.uleb128 0x45
	.4byte	.LASF621
	.byte	0x3
	.2byte	0x32f
	.4byte	0x30c
	.4byte	.LLST270
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x330
	.4byte	0x30c
	.4byte	.LLST271
	.uleb128 0x52
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x330
	.4byte	0x30c
	.4byte	.LLST272
	.uleb128 0x52
	.ascii	"n\000"
	.byte	0x3
	.2byte	0x332
	.4byte	0x30c
	.4byte	.LLST273
	.uleb128 0x45
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x332
	.4byte	0x30c
	.4byte	.LLST274
	.uleb128 0x45
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x332
	.4byte	0x30c
	.4byte	.LLST275
	.uleb128 0x45
	.4byte	.LASF622
	.byte	0x3
	.2byte	0x333
	.4byte	0x30c
	.4byte	.LLST276
	.uleb128 0x3e
	.4byte	.LASF623
	.byte	0x3
	.2byte	0x333
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF624
	.byte	0x3
	.2byte	0x334
	.4byte	0x30c
	.4byte	.LLST277
	.uleb128 0x45
	.4byte	.LASF625
	.byte	0x3
	.2byte	0x335
	.4byte	0x30c
	.4byte	.LLST278
	.uleb128 0x53
	.4byte	.LBB513
	.4byte	.LBE513-.LBB513
	.4byte	0x5211
	.uleb128 0x3e
	.4byte	.LASF392
	.byte	0x3
	.2byte	0x379
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL1414
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL1415
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB514
	.4byte	.LBE514-.LBB514
	.byte	0x3
	.2byte	0x34f
	.4byte	0x526b
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB515
	.4byte	.LBE515-.LBB515
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB516
	.4byte	.LBE516-.LBB516
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL1419
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1374
	.4byte	0x377a
	.uleb128 0x31
	.4byte	.LVL1380
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1395
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1398
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1405
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1411
	.4byte	0x176f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF626
	.byte	0x3
	.2byte	0x325
	.4byte	0x64
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52c6
	.uleb128 0x31
	.4byte	.LVL1210
	.4byte	0x52c6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF627
	.byte	0x3
	.2byte	0x31c
	.4byte	0x64
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5305
	.uleb128 0x31
	.4byte	.LVL1206
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1207
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1208
	.4byte	0x372d
	.uleb128 0x31
	.4byte	.LVL1209
	.4byte	0x3588
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF628
	.byte	0x3
	.2byte	0x2e6
	.4byte	0x64
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5394
	.uleb128 0x3e
	.4byte	.LASF629
	.byte	0x3
	.2byte	0x2e8
	.4byte	0x5394
	.uleb128 0x40
	.4byte	.LASF630
	.byte	0x3
	.2byte	0x2f3
	.uleb128 0x53
	.4byte	.LBB526
	.4byte	.LBE526-.LBB526
	.4byte	0x5354
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x310
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL1629
	.4byte	0x1c72
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1622
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1623
	.4byte	0x5868
	.uleb128 0x31
	.4byte	.LVL1624
	.4byte	0x56e0
	.uleb128 0x31
	.4byte	.LVL1625
	.4byte	0x55e4
	.uleb128 0x31
	.4byte	.LVL1626
	.4byte	0x4407
	.uleb128 0x31
	.4byte	.LVL1627
	.4byte	0x4834
	.uleb128 0x31
	.4byte	.LVL1628
	.4byte	0x2805
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x419
	.uleb128 0x4d
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x26e
	.4byte	0x64
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5493
	.uleb128 0x3e
	.4byte	.LASF500
	.byte	0x3
	.2byte	0x270
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF632
	.byte	0x3
	.2byte	0x270
	.4byte	0x301
	.4byte	.LLST298
	.uleb128 0x45
	.4byte	.LASF633
	.byte	0x3
	.2byte	0x271
	.4byte	0x301
	.4byte	.LLST299
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x272
	.4byte	0x301
	.uleb128 0x48
	.4byte	.LASF634
	.byte	0x3
	.2byte	0x2bc
	.4byte	.L1779
	.uleb128 0x48
	.4byte	.LASF635
	.byte	0x3
	.2byte	0x2cc
	.4byte	.L1781
	.uleb128 0x31
	.4byte	.LVL1591
	.4byte	0x4407
	.uleb128 0x31
	.4byte	.LVL1592
	.4byte	0x4834
	.uleb128 0x31
	.4byte	.LVL1593
	.4byte	0x460c
	.uleb128 0x31
	.4byte	.LVL1600
	.4byte	0x499b
	.uleb128 0x31
	.4byte	.LVL1601
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1602
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1603
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1605
	.4byte	0x5493
	.uleb128 0x31
	.4byte	.LVL1609
	.4byte	0x58b9
	.uleb128 0x31
	.4byte	.LVL1610
	.4byte	0x4407
	.uleb128 0x31
	.4byte	.LVL1612
	.4byte	0x5493
	.uleb128 0x31
	.4byte	.LVL1614
	.4byte	0x5493
	.uleb128 0x31
	.4byte	.LVL1618
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1619
	.4byte	0x4250
	.uleb128 0x31
	.4byte	.LVL1620
	.4byte	0x3588
	.uleb128 0x31
	.4byte	.LVL1621
	.4byte	0x2805
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF636
	.byte	0x3
	.2byte	0x20f
	.4byte	0x64
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x55ba
	.uleb128 0x44
	.4byte	.LASF500
	.byte	0x3
	.2byte	0x20f
	.4byte	0x301
	.4byte	.LLST100
	.uleb128 0x44
	.4byte	.LASF637
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2f6
	.4byte	.LLST101
	.uleb128 0x45
	.4byte	.LASF632
	.byte	0x3
	.2byte	0x211
	.4byte	0x301
	.4byte	.LLST102
	.uleb128 0x3e
	.4byte	.LASF357
	.byte	0x3
	.2byte	0x211
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x3
	.2byte	0x212
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x3
	.2byte	0x212
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF638
	.byte	0x3
	.2byte	0x213
	.4byte	0x301
	.4byte	.LLST103
	.uleb128 0x45
	.4byte	.LASF639
	.byte	0x3
	.2byte	0x214
	.4byte	0x301
	.4byte	.LLST104
	.uleb128 0x45
	.4byte	.LASF640
	.byte	0x3
	.2byte	0x215
	.4byte	0x301
	.4byte	.LLST105
	.uleb128 0x45
	.4byte	.LASF641
	.byte	0x3
	.2byte	0x216
	.4byte	0x301
	.4byte	.LLST106
	.uleb128 0x48
	.4byte	.LASF642
	.byte	0x3
	.2byte	0x240
	.4byte	.L495
	.uleb128 0x31
	.4byte	.LVL418
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL425
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL433
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL437
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL438
	.4byte	0x55ba
	.uleb128 0x31
	.4byte	.LVL439
	.4byte	0x4afe
	.uleb128 0x31
	.4byte	.LVL444
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL449
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL450
	.4byte	0x55ba
	.uleb128 0x31
	.4byte	.LVL451
	.4byte	0x4afe
	.uleb128 0x31
	.4byte	.LVL454
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL456
	.4byte	0x436e
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF643
	.byte	0x3
	.2byte	0x201
	.4byte	0x64
	.byte	0x1
	.4byte	0x55e4
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x3
	.2byte	0x201
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF644
	.byte	0x3
	.2byte	0x203
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF645
	.byte	0x3
	.2byte	0x1ec
	.4byte	0x64
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x56e0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB408
	.4byte	.LBE408-.LBB408
	.byte	0x3
	.2byte	0x1f9
	.4byte	0x5658
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB409
	.4byte	.LBE409-.LBB409
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB410
	.4byte	.LBE410-.LBB410
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL747
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB411
	.4byte	.LBE411-.LBB411
	.byte	0x3
	.2byte	0x1fa
	.4byte	0x56b2
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB412
	.4byte	.LBE412-.LBB412
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB413
	.4byte	.LBE413-.LBB413
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL748
	.4byte	0x672a
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL744
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL745
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL746
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL749
	.4byte	0x230f
	.uleb128 0x31
	.4byte	.LVL750
	.4byte	0x31bc
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF646
	.byte	0x3
	.2byte	0x116
	.4byte	0x64
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5868
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x118
	.4byte	0x30c
	.4byte	.LLST114
	.uleb128 0x3e
	.4byte	.LASF647
	.byte	0x3
	.2byte	0x119
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x48f4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6287
	.uleb128 0x31
	.4byte	.LVL479
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL480
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL481
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL482
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL483
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL484
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL485
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL486
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL487
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL488
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL489
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL490
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL491
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL492
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL493
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL494
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL495
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL496
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL497
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL498
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL499
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL500
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL501
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL502
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL503
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL504
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL505
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL506
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL507
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL508
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL509
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL510
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL511
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL512
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL513
	.4byte	0x6740
	.uleb128 0x31
	.4byte	.LVL516
	.4byte	0x671f
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF648
	.byte	0x3
	.byte	0xb6
	.4byte	0x64
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x58b9
	.uleb128 0x23
	.4byte	.LASF629
	.byte	0x3
	.byte	0xb6
	.4byte	0x5394
	.4byte	.LLST11
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x3
	.byte	0xb8
	.4byte	0x30c
	.4byte	.LLST12
	.uleb128 0x31
	.4byte	.LVL28
	.4byte	0x58fa
	.uleb128 0x31
	.4byte	.LVL29
	.4byte	0x58fa
	.uleb128 0x31
	.4byte	.LVL32
	.4byte	0x58b9
	.byte	0
	.uleb128 0x22
	.4byte	.LASF649
	.byte	0x3
	.byte	0xa4
	.4byte	0x64
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x58e2
	.uleb128 0x23
	.4byte	.LASF650
	.byte	0x3
	.byte	0xa4
	.4byte	0x301
	.4byte	.LLST10
	.byte	0
	.uleb128 0x58
	.4byte	.LASF651
	.byte	0x3
	.byte	0x9e
	.byte	0x1
	.4byte	0x58fa
	.uleb128 0x34
	.4byte	.LASF652
	.byte	0x3
	.byte	0x9e
	.4byte	0x30c
	.byte	0
	.uleb128 0x22
	.4byte	.LASF653
	.byte	0x3
	.byte	0x7f
	.4byte	0x301
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5939
	.uleb128 0x2b
	.ascii	"num\000"
	.byte	0x3
	.byte	0x7f
	.4byte	0x30c
	.4byte	.LLST9
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x3
	.byte	0x81
	.4byte	0x301
	.uleb128 0x20
	.4byte	.LASF654
	.byte	0x3
	.byte	0x82
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x52
	.byte	0
	.uleb128 0x22
	.4byte	.LASF655
	.byte	0x3
	.byte	0x71
	.4byte	0x64
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x597f
	.uleb128 0x23
	.4byte	.LASF85
	.byte	0x3
	.byte	0x71
	.4byte	0xb6
	.4byte	.LLST67
	.uleb128 0x29
	.ascii	"buf\000"
	.byte	0x3
	.byte	0x73
	.4byte	0xb6
	.uleb128 0x31
	.4byte	.LVL286
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL287
	.4byte	0x597f
	.byte	0
	.uleb128 0x22
	.4byte	.LASF656
	.byte	0x3
	.byte	0xa
	.4byte	0x64
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c75
	.uleb128 0x23
	.4byte	.LASF85
	.byte	0x3
	.byte	0xa
	.4byte	0xb6
	.4byte	.LLST64
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x3
	.byte	0xc
	.4byte	0x64
	.uleb128 0x29
	.ascii	"buf\000"
	.byte	0x3
	.byte	0xd
	.4byte	0xb6
	.uleb128 0x53
	.4byte	.LBB284
	.4byte	.LBE284-.LBB284
	.4byte	0x59ea
	.uleb128 0x2c
	.4byte	.LASF392
	.byte	0x3
	.byte	0x57
	.4byte	0x301
	.4byte	.LLST65
	.uleb128 0x31
	.4byte	.LVL277
	.4byte	0x3fa6
	.uleb128 0x31
	.4byte	.LVL279
	.4byte	0x674b
	.byte	0
	.uleb128 0x53
	.4byte	.LBB285
	.4byte	.LBE285-.LBB285
	.4byte	0x5a10
	.uleb128 0x2c
	.4byte	.LASF392
	.byte	0x3
	.byte	0x60
	.4byte	0x301
	.4byte	.LLST66
	.uleb128 0x31
	.4byte	.LVL283
	.4byte	0x674b
	.byte	0
	.uleb128 0x31
	.4byte	.LVL207
	.4byte	0x6757
	.uleb128 0x31
	.4byte	.LVL208
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL209
	.4byte	0x6757
	.uleb128 0x31
	.4byte	.LVL210
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL211
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL212
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL213
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL214
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL215
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL216
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL217
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL218
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL219
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL220
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL221
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL222
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL223
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL224
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL225
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL226
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL227
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL228
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL229
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL230
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL231
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL232
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL233
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL234
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL235
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL236
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL237
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL238
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL239
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL240
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL241
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL242
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL243
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL244
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL245
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL246
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL247
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL248
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL249
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL250
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL251
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL252
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL253
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL254
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL255
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL256
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL257
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL258
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL259
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL260
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL261
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL262
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL263
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL264
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL265
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL266
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL267
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL268
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL269
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL270
	.4byte	0x277b
	.uleb128 0x31
	.4byte	.LVL271
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL272
	.4byte	0x272c
	.uleb128 0x31
	.4byte	.LVL273
	.4byte	0x674b
	.uleb128 0x31
	.4byte	.LVL276
	.4byte	0x674b
	.byte	0
	.uleb128 0x60
	.4byte	0x3409
	.4byte	.LFB286
	.4byte	.LFE286-.LFB286
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c97
	.uleb128 0x61
	.4byte	0x3416
	.4byte	.LLST8
	.uleb128 0x2e
	.4byte	0x3422
	.byte	0
	.uleb128 0x60
	.4byte	0x58e2
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5cb2
	.uleb128 0x62
	.4byte	0x58ee
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x60
	.4byte	0x55ba
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5cea
	.uleb128 0x61
	.4byte	0x55cb
	.4byte	.LLST13
	.uleb128 0x30
	.4byte	0x55d7
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x2e
	.4byte	0x55cb
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x30
	.4byte	0x5cce
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x63
	.4byte	0x4af6
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x63
	.4byte	0x43fb
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x63
	.4byte	0x43ef
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x60
	.4byte	0x4206
	.4byte	.LFB195
	.4byte	.LFE195-.LFB195
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5d65
	.uleb128 0x61
	.4byte	0x4212
	.4byte	.LLST37
	.uleb128 0x30
	.4byte	0x421d
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x68
	.4byte	0x5d5b
	.uleb128 0x2e
	.4byte	0x4212
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x68
	.uleb128 0x30
	.4byte	0x5d33
	.uleb128 0x24
	.4byte	.LVL120
	.4byte	0x671f
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL119
	.4byte	0x4178
	.byte	0
	.uleb128 0x60
	.4byte	0x422b
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5db3
	.uleb128 0x61
	.4byte	0x4237
	.4byte	.LLST40
	.uleb128 0x30
	.4byte	0x4242
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x80
	.4byte	0x5da9
	.uleb128 0x2e
	.4byte	0x4237
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x80
	.uleb128 0x30
	.4byte	0x5d81
	.uleb128 0x24
	.4byte	.LVL130
	.4byte	0x671f
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL129
	.4byte	0x4109
	.byte	0
	.uleb128 0x60
	.4byte	0x3f27
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5e69
	.uleb128 0x61
	.4byte	0x3f38
	.4byte	.LLST49
	.uleb128 0x30
	.4byte	0x3f44
	.uleb128 0x30
	.4byte	0x3f50
	.uleb128 0x30
	.4byte	0x3f5c
	.uleb128 0x30
	.4byte	0x3f68
	.uleb128 0x30
	.4byte	0x3f74
	.uleb128 0x30
	.4byte	0x3f80
	.uleb128 0x30
	.4byte	0x3f8c
	.uleb128 0x30
	.4byte	0x3f98
	.uleb128 0x33
	.4byte	.LBB268
	.4byte	.LBE268-.LBB268
	.uleb128 0x2e
	.4byte	0x3f38
	.uleb128 0x33
	.4byte	.LBB269
	.4byte	.LBE269-.LBB269
	.uleb128 0x30
	.4byte	0x5dcf
	.uleb128 0x30
	.4byte	0x5dd4
	.uleb128 0x64
	.4byte	0x5dd9
	.4byte	.LLST50
	.uleb128 0x30
	.4byte	0x5dde
	.uleb128 0x64
	.4byte	0x5de3
	.4byte	.LLST51
	.uleb128 0x64
	.4byte	0x5de8
	.4byte	.LLST52
	.uleb128 0x30
	.4byte	0x5ded
	.uleb128 0x30
	.4byte	0x5df2
	.uleb128 0x31
	.4byte	.LVL163
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL165
	.4byte	0x4087
	.uleb128 0x31
	.4byte	.LVL166
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL167
	.4byte	0x4206
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x3d77
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5ed7
	.uleb128 0x61
	.4byte	0x3d88
	.4byte	.LLST53
	.uleb128 0x64
	.4byte	0x3d94
	.4byte	.LLST54
	.uleb128 0x30
	.4byte	0x3da0
	.uleb128 0x30
	.4byte	0x3dac
	.uleb128 0x53
	.4byte	.LBB272
	.4byte	.LBE272-.LBB272
	.4byte	0x5ecd
	.uleb128 0x2e
	.4byte	0x3d88
	.uleb128 0x33
	.4byte	.LBB273
	.4byte	.LBE273-.LBB273
	.uleb128 0x30
	.4byte	0x5e85
	.uleb128 0x30
	.4byte	0x5e8e
	.uleb128 0x30
	.4byte	0x5e93
	.uleb128 0x31
	.4byte	.LVL171
	.4byte	0x671f
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL172
	.4byte	0x4250
	.byte	0
	.uleb128 0x60
	.4byte	0x3709
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f0f
	.uleb128 0x61
	.4byte	0x3716
	.4byte	.LLST55
	.uleb128 0x30
	.4byte	0x3722
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x98
	.uleb128 0x2e
	.4byte	0x3716
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x98
	.uleb128 0x30
	.4byte	0x5ef3
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x3409
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f3e
	.uleb128 0x61
	.4byte	0x3416
	.4byte	.LLST56
	.uleb128 0x61
	.4byte	0x3422
	.4byte	.LLST57
	.uleb128 0x24
	.4byte	.LVL178
	.4byte	0x5c75
	.byte	0
	.uleb128 0x60
	.4byte	0x2d01
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f7e
	.uleb128 0x62
	.4byte	0x2d0e
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x2d1a
	.uleb128 0x33
	.4byte	.LBB282
	.4byte	.LBE282-.LBB282
	.uleb128 0x2e
	.4byte	0x2d0e
	.uleb128 0x33
	.4byte	.LBB283
	.4byte	.LBE283-.LBB283
	.uleb128 0x65
	.4byte	0x5f58
	.uleb128 0x1
	.byte	0x51
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x2674
	.4byte	.LFB247
	.4byte	.LFE247-.LFB247
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fb3
	.uleb128 0x61
	.4byte	0x2685
	.4byte	.LLST72
	.uleb128 0x33
	.4byte	.LBB288
	.4byte	.LBE288-.LBB288
	.uleb128 0x2e
	.4byte	0x2685
	.uleb128 0x31
	.4byte	.LVL308
	.4byte	0x422b
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x1e04
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fd9
	.uleb128 0x31
	.4byte	.LVL345
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL346
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x60
	.4byte	0x1900
	.4byte	.LFB275
	.4byte	.LFE275-.LFB275
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x60ee
	.uleb128 0x61
	.4byte	0x1910
	.4byte	.LLST107
	.uleb128 0x30
	.4byte	0x191b
	.uleb128 0x30
	.4byte	0x1926
	.uleb128 0x30
	.4byte	0x1931
	.uleb128 0x33
	.4byte	.LBB313
	.4byte	.LBE313-.LBB313
	.uleb128 0x2e
	.4byte	0x1910
	.uleb128 0x33
	.4byte	.LBB314
	.4byte	.LBE314-.LBB314
	.uleb128 0x30
	.4byte	0x5ff5
	.uleb128 0x65
	.4byte	0x5ffa
	.uleb128 0x3
	.byte	0x91
	.sleb128 -80
	.uleb128 0x65
	.4byte	0x5fff
	.uleb128 0x3
	.byte	0x91
	.sleb128 -100
	.uleb128 0x66
	.4byte	0x193c
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB315
	.4byte	.Ldebug_ranges0+0xd0
	.byte	0x1
	.byte	0xa7
	.4byte	0x6088
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xd0
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xd0
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL459
	.4byte	0x6714
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB321
	.4byte	.Ldebug_ranges0+0xe8
	.byte	0x1
	.byte	0xa8
	.4byte	0x60d0
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xe8
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xe8
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL460
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL461
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL462
	.4byte	0x1954
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x17c6
	.4byte	.LFB277
	.4byte	.LFE277-.LFB277
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x616b
	.uleb128 0x61
	.4byte	0x17d6
	.4byte	.LLST111
	.uleb128 0x61
	.4byte	0x17df
	.4byte	.LLST112
	.uleb128 0x61
	.4byte	0x17e8
	.4byte	.LLST113
	.uleb128 0x53
	.4byte	.LBB353
	.4byte	.LBE353-.LBB353
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.byte	0
	.uleb128 0x33
	.4byte	.LBB354
	.4byte	.LBE354-.LBB354
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB355
	.4byte	.LBE355-.LBB355
	.uleb128 0x30
	.4byte	0x6129
	.uleb128 0x30
	.4byte	0x612e
	.uleb128 0x31
	.4byte	.LVL478
	.4byte	0x6714
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x1d5a
	.4byte	.LFB268
	.4byte	.LFE268-.LFB268
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6209
	.uleb128 0x65
	.4byte	0x1d6b
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x1d77
	.uleb128 0x30
	.4byte	0x1d81
	.uleb128 0x30
	.4byte	0x1d8d
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x150
	.4byte	0x61f5
	.uleb128 0x30
	.4byte	0x617e
	.uleb128 0x30
	.4byte	0x6185
	.uleb128 0x64
	.4byte	0x618a
	.4byte	.LLST146
	.uleb128 0x65
	.4byte	0x618f
	.uleb128 0x3
	.byte	0x91
	.sleb128 -80
	.uleb128 0x53
	.4byte	.LBB376
	.4byte	.LBE376-.LBB376
	.4byte	0x61eb
	.uleb128 0x65
	.4byte	0x1d9e
	.uleb128 0x3
	.byte	0x91
	.sleb128 -100
	.uleb128 0x31
	.4byte	.LVL642
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL643
	.4byte	0x4561
	.uleb128 0x31
	.4byte	.LVL644
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x31
	.4byte	.LVL641
	.4byte	0x3929
	.byte	0
	.uleb128 0x33
	.4byte	.LBB379
	.4byte	.LBE379-.LBB379
	.uleb128 0x64
	.4byte	0x1dac
	.4byte	.LLST147
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x1dba
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6294
	.uleb128 0x61
	.4byte	0x1dcb
	.4byte	.LLST188
	.uleb128 0x30
	.4byte	0x1dd7
	.uleb128 0x30
	.4byte	0x1de3
	.uleb128 0x30
	.4byte	0x1ded
	.uleb128 0x30
	.4byte	0x1df7
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1a0
	.uleb128 0x2e
	.4byte	0x1dcb
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1a0
	.uleb128 0x30
	.4byte	0x6225
	.uleb128 0x65
	.4byte	0x622a
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x64
	.4byte	0x622f
	.4byte	.LLST189
	.uleb128 0x65
	.4byte	0x6234
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x31
	.4byte	.LVL867
	.4byte	0x1e04
	.uleb128 0x31
	.4byte	.LVL869
	.4byte	0x45c9
	.uleb128 0x31
	.4byte	.LVL870
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL871
	.4byte	0x4b59
	.uleb128 0x31
	.4byte	.LVL872
	.4byte	0x499b
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x23a4
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x630e
	.uleb128 0x61
	.4byte	0x23b5
	.4byte	.LLST190
	.uleb128 0x30
	.4byte	0x23c1
	.uleb128 0x53
	.4byte	.LBB440
	.4byte	.LBE440-.LBB440
	.4byte	0x6304
	.uleb128 0x2e
	.4byte	0x23b5
	.uleb128 0x33
	.4byte	.LBB441
	.4byte	.LBE441-.LBB441
	.uleb128 0x30
	.4byte	0x62b0
	.uleb128 0x31
	.4byte	.LVL877
	.4byte	0x4087
	.uleb128 0x31
	.4byte	.LVL878
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL879
	.4byte	0x2674
	.uleb128 0x31
	.4byte	.LVL880
	.4byte	0x1dba
	.uleb128 0x31
	.4byte	.LVL881
	.4byte	0x671f
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL884
	.4byte	0x3f27
	.byte	0
	.uleb128 0x60
	.4byte	0x364f
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6386
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1b8
	.uleb128 0x64
	.4byte	0x365d
	.4byte	.LLST192
	.uleb128 0x64
	.4byte	0x3669
	.4byte	.LLST193
	.uleb128 0x64
	.4byte	0x3675
	.4byte	.LLST194
	.uleb128 0x65
	.4byte	0x3681
	.uleb128 0x2
	.byte	0x91
	.sleb128 -52
	.uleb128 0x30
	.4byte	0x368d
	.uleb128 0x31
	.4byte	.LVL892
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL893
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL896
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL898
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL899
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL900
	.4byte	0x232e
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x2b73
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x63a3
	.uleb128 0x24
	.4byte	.LVL1107
	.4byte	0x3850
	.byte	0
	.uleb128 0x60
	.4byte	0x342f
	.4byte	.LFB221
	.4byte	.LFE221-.LFB221
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x63d2
	.uleb128 0x31
	.4byte	.LVL1135
	.4byte	0x377a
	.uleb128 0x31
	.4byte	.LVL1136
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1137
	.4byte	0x17c6
	.byte	0
	.uleb128 0x60
	.4byte	0x1f6f
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6401
	.uleb128 0x31
	.4byte	.LVL1262
	.4byte	0x1f78
	.uleb128 0x31
	.4byte	.LVL1263
	.4byte	0x369b
	.uleb128 0x31
	.4byte	.LVL1264
	.4byte	0x20c4
	.byte	0
	.uleb128 0x60
	.4byte	0x1c72
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x65f2
	.uleb128 0x61
	.4byte	0x1c83
	.4byte	.LLST255
	.uleb128 0x61
	.4byte	0x1c8f
	.4byte	.LLST256
	.uleb128 0x30
	.4byte	0x1c9b
	.uleb128 0x30
	.4byte	0x1ca7
	.uleb128 0x30
	.4byte	0x1cb3
	.uleb128 0x30
	.4byte	0x1cbf
	.uleb128 0x30
	.4byte	0x1ccb
	.uleb128 0x30
	.4byte	0x1cd7
	.uleb128 0x30
	.4byte	0x1ce3
	.uleb128 0x30
	.4byte	0x1cef
	.uleb128 0x30
	.4byte	0x1cfb
	.uleb128 0x30
	.4byte	0x1d07
	.uleb128 0x30
	.4byte	0x1d23
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x208
	.4byte	0x65e8
	.uleb128 0x2e
	.4byte	0x1c8f
	.uleb128 0x2e
	.4byte	0x1c83
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x208
	.uleb128 0x64
	.4byte	0x6426
	.4byte	.LLST257
	.uleb128 0x64
	.4byte	0x642b
	.4byte	.LLST258
	.uleb128 0x64
	.4byte	0x6430
	.4byte	.LLST259
	.uleb128 0x30
	.4byte	0x6435
	.uleb128 0x30
	.4byte	0x643a
	.uleb128 0x30
	.4byte	0x643f
	.uleb128 0x64
	.4byte	0x6444
	.4byte	.LLST260
	.uleb128 0x64
	.4byte	0x6449
	.4byte	.LLST261
	.uleb128 0x64
	.4byte	0x644e
	.4byte	.LLST262
	.uleb128 0x64
	.4byte	0x6453
	.4byte	.LLST263
	.uleb128 0x66
	.4byte	0x1d13
	.uleb128 0x66
	.4byte	0x1d1b
	.uleb128 0x30
	.4byte	0x6458
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x230
	.4byte	0x650e
	.uleb128 0x30
	.4byte	0x1d31
	.uleb128 0x31
	.4byte	.LVL1289
	.4byte	0x2692
	.uleb128 0x31
	.4byte	.LVL1290
	.4byte	0x3fa6
	.uleb128 0x31
	.4byte	.LVL1291
	.4byte	0x3fa6
	.uleb128 0x31
	.4byte	.LVL1292
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1294
	.4byte	0x1e04
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1306
	.4byte	0x2255
	.uleb128 0x31
	.4byte	.LVL1307
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1312
	.4byte	0x3fa6
	.uleb128 0x31
	.4byte	.LVL1315
	.4byte	0x1d5a
	.uleb128 0x31
	.4byte	.LVL1316
	.4byte	0x3fa6
	.uleb128 0x31
	.4byte	.LVL1317
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1321
	.4byte	0x2255
	.uleb128 0x31
	.4byte	.LVL1322
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1323
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1324
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1325
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1326
	.4byte	0x2c45
	.uleb128 0x31
	.4byte	.LVL1328
	.4byte	0x1d5a
	.uleb128 0x31
	.4byte	.LVL1343
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1345
	.4byte	0x671f
	.uleb128 0x31
	.4byte	.LVL1347
	.4byte	0x3929
	.uleb128 0x31
	.4byte	.LVL1348
	.4byte	0x1f15
	.uleb128 0x31
	.4byte	.LVL1351
	.4byte	0x2287
	.uleb128 0x31
	.4byte	.LVL1352
	.4byte	0x1ea1
	.uleb128 0x31
	.4byte	.LVL1359
	.4byte	0x1ea1
	.uleb128 0x31
	.4byte	.LVL1360
	.4byte	0x23a4
	.uleb128 0x31
	.4byte	.LVL1361
	.4byte	0x4f66
	.uleb128 0x31
	.4byte	.LVL1362
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1363
	.4byte	0x3588
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1293
	.4byte	0x20c4
	.byte	0
	.uleb128 0x60
	.4byte	0x2805
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6700
	.uleb128 0x30
	.4byte	0x2816
	.uleb128 0x53
	.4byte	.LBB523
	.4byte	.LBE523-.LBB523
	.4byte	0x669c
	.uleb128 0x64
	.4byte	0x6605
	.4byte	.LLST297
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x268
	.4byte	0x662f
	.uleb128 0x30
	.4byte	0x2821
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1577
	.4byte	0x2a84
	.uleb128 0x31
	.4byte	.LVL1578
	.4byte	0x2a84
	.uleb128 0x31
	.4byte	.LVL1579
	.4byte	0x369b
	.uleb128 0x31
	.4byte	.LVL1580
	.4byte	0x369b
	.uleb128 0x31
	.4byte	.LVL1581
	.4byte	0x39f4
	.uleb128 0x31
	.4byte	.LVL1582
	.4byte	0x3588
	.uleb128 0x31
	.4byte	.LVL1583
	.4byte	0x3588
	.uleb128 0x31
	.4byte	.LVL1586
	.4byte	0x1e6d
	.uleb128 0x31
	.4byte	.LVL1587
	.4byte	0x1e6d
	.uleb128 0x31
	.4byte	.LVL1588
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1589
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1590
	.4byte	0x29c8
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1565
	.4byte	0x4407
	.uleb128 0x31
	.4byte	.LVL1566
	.4byte	0x343c
	.uleb128 0x31
	.4byte	.LVL1567
	.4byte	0x332f
	.uleb128 0x31
	.4byte	.LVL1568
	.4byte	0x318f
	.uleb128 0x31
	.4byte	.LVL1569
	.4byte	0x31f1
	.uleb128 0x31
	.4byte	.LVL1570
	.4byte	0x2af9
	.uleb128 0x31
	.4byte	.LVL1571
	.4byte	0x342f
	.uleb128 0x31
	.4byte	.LVL1572
	.4byte	0x42fc
	.uleb128 0x31
	.4byte	.LVL1573
	.4byte	0x2b7c
	.uleb128 0x31
	.4byte	.LVL1574
	.4byte	0x2cb0
	.uleb128 0x31
	.4byte	.LVL1575
	.4byte	0x3709
	.byte	0
	.uleb128 0x67
	.4byte	.LASF657
	.4byte	.LASF657
	.byte	0x11
	.byte	0x6f
	.uleb128 0x68
	.4byte	.LASF670
	.4byte	.LASF670
	.uleb128 0x67
	.4byte	.LASF658
	.4byte	.LASF658
	.byte	0x12
	.byte	0x19
	.uleb128 0x67
	.4byte	.LASF659
	.4byte	.LASF659
	.byte	0xb
	.byte	0x8e
	.uleb128 0x67
	.4byte	.LASF660
	.4byte	.LASF660
	.byte	0x12
	.byte	0x1b
	.uleb128 0x67
	.4byte	.LASF661
	.4byte	.LASF661
	.byte	0x13
	.byte	0x27
	.uleb128 0x67
	.4byte	.LASF662
	.4byte	.LASF662
	.byte	0xd
	.byte	0x18
	.uleb128 0x69
	.4byte	.LASF663
	.4byte	.LASF663
	.byte	0xc
	.2byte	0x19b
	.uleb128 0x6a
	.4byte	.LASF671
	.4byte	.LASF672
	.byte	0x14
	.byte	0
	.4byte	.LASF671
	.byte	0
	.section	.debug_abbrev,"",%progbits
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
	.uleb128 0x6
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
	.uleb128 0xe
	.byte	0
	.byte	0
	.uleb128 0x4
	.uleb128 0x26
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x5
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
	.uleb128 0x8
	.uleb128 0xf
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x9
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
	.uleb128 0xa
	.uleb128 0x15
	.byte	0
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0xb
	.uleb128 0x15
	.byte	0
	.uleb128 0x27
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0xc
	.uleb128 0x1
	.byte	0x1
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0xd
	.uleb128 0x21
	.byte	0
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
	.uleb128 0x6
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
	.uleb128 0x2115
	.uleb128 0x19
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
	.uleb128 0x27
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
	.uleb128 0x28
	.uleb128 0xb
	.byte	0x1
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
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2116
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2b
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
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x2d
	.uleb128 0x1d
	.byte	0x1
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
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2e
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x2f
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x55
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x30
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x31
	.uleb128 0x4109
	.byte	0
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x32
	.uleb128 0x1d
	.byte	0x1
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x33
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
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
	.uleb128 0xb
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
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x36
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
	.uleb128 0x37
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
	.uleb128 0x38
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
	.uleb128 0x39
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x55
	.uleb128 0x17
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x3a
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
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2116
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x3b
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
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x3c
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
	.uleb128 0x3d
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
	.byte	0
	.byte	0
	.uleb128 0x3f
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
	.uleb128 0x40
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
	.uleb128 0x41
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
	.uleb128 0x42
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x43
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
	.uleb128 0x44
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
	.uleb128 0x45
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
	.uleb128 0x46
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
	.uleb128 0x47
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
	.uleb128 0x48
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
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
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
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x4b
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
	.uleb128 0x4c
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
	.uleb128 0x4d
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
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2116
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x4e
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
	.uleb128 0x4f
	.uleb128 0x1d
	.byte	0x1
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x52
	.uleb128 0x1
	.uleb128 0x55
	.uleb128 0x17
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0x5
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x50
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
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
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
	.uleb128 0x5
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x52
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
	.uleb128 0x53
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x54
	.uleb128 0x1d
	.byte	0x1
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0x5
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x55
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
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2116
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x56
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
	.uleb128 0x57
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
	.uleb128 0x58
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
	.uleb128 0x59
	.uleb128 0x1d
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x58
	.uleb128 0xb
	.uleb128 0x59
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x5a
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
	.uleb128 0x5b
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
	.uleb128 0x5c
	.uleb128 0x1d
	.byte	0x1
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
	.uleb128 0x5d
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
	.uleb128 0x5e
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
	.uleb128 0x5f
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
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0x60
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x61
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x62
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x63
	.uleb128 0x2e
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x2117
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0x64
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x65
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x66
	.uleb128 0xa
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x67
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
	.uleb128 0x68
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
	.uleb128 0x69
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
	.uleb128 0x6a
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
	.section	.debug_loc,"",%progbits
.Ldebug_loc0:
.LLST300:
	.4byte	.LVL1630-.Ltext0
	.4byte	.LVL1631-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST301:
	.4byte	.LVL1630-.Ltext0
	.4byte	.LVL1631-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST302:
	.4byte	.LVL1630-.Ltext0
	.4byte	.LVL1631-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST176:
	.4byte	.LVL812-.Ltext0
	.4byte	.LVL813-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST177:
	.4byte	.LVL812-.Ltext0
	.4byte	.LVL813-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST178:
	.4byte	.LVL812-.Ltext0
	.4byte	.LVL813-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST108:
	.4byte	.LVL466-.Ltext0
	.4byte	.LVL468-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL468-1-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST109:
	.4byte	.LVL466-.Ltext0
	.4byte	.LVL467-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL467-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST110:
	.4byte	.LVL470-.Ltext0
	.4byte	.LVL471-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL473-.Ltext0
	.4byte	.LVL475-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL476-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST95:
	.4byte	.LVL399-.Ltext0
	.4byte	.LVL400-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL401-.Ltext0
	.4byte	.LVL404-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL405-.Ltext0
	.4byte	.LVL406-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST96:
	.4byte	.LVL399-.Ltext0
	.4byte	.LVL400-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST97:
	.4byte	.LVL399-.Ltext0
	.4byte	.LVL400-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL400-.Ltext0
	.4byte	.LVL405-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL405-.Ltext0
	.4byte	.LFE274-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST90:
	.4byte	.LVL378-.Ltext0
	.4byte	.LVL380-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST91:
	.4byte	.LVL378-.Ltext0
	.4byte	.LVL384-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL384-.Ltext0
	.4byte	.LVL392-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL396-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST92:
	.4byte	.LVL378-.Ltext0
	.4byte	.LVL384-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL384-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST93:
	.4byte	.LVL378-.Ltext0
	.4byte	.LVL379-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL383-.Ltext0
	.4byte	.LVL384-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL389-.Ltext0
	.4byte	.LVL390-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL396-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST94:
	.4byte	.LVL381-.Ltext0
	.4byte	.LVL389-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL397-.Ltext0
	.4byte	.LVL398-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST83:
	.4byte	.LVL347-.Ltext0
	.4byte	.LVL351-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL352-.Ltext0
	.4byte	.LVL354-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL355-.Ltext0
	.4byte	.LVL357-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST84:
	.4byte	.LVL347-.Ltext0
	.4byte	.LVL352-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL352-.Ltext0
	.4byte	.LFE272-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST85:
	.4byte	.LVL347-.Ltext0
	.4byte	.LVL353-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL355-.Ltext0
	.4byte	.LVL356-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST0:
	.4byte	.LVL0-.Ltext0
	.4byte	.LVL5-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST1:
	.4byte	.LVL0-.Ltext0
	.4byte	.LVL7-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL7-.Ltext0
	.4byte	.LVL14-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST2:
	.4byte	.LVL0-.Ltext0
	.4byte	.LVL9-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL9-1-.Ltext0
	.4byte	.LVL14-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST3:
	.4byte	.LVL2-.Ltext0
	.4byte	.LVL11-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST4:
	.4byte	.LVL3-.Ltext0
	.4byte	.LVL6-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL8-.Ltext0
	.4byte	.LVL14-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST5:
	.4byte	.LVL10-.Ltext0
	.4byte	.LVL13-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST6:
	.4byte	.LVL1-.Ltext0
	.4byte	.LVL4-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST7:
	.4byte	.LVL11-.Ltext0
	.4byte	.LVL12-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST82:
	.4byte	.LVL340-.Ltext0
	.4byte	.LVL341-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL341-1-.Ltext0
	.4byte	.LVL344-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST81:
	.4byte	.LVL336-.Ltext0
	.4byte	.LVL337-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL337-.Ltext0
	.4byte	.LFE264-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST254:
	.4byte	.LVL1277-.Ltext0
	.4byte	.LVL1278-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST167:
	.4byte	.LVL770-.Ltext0
	.4byte	.LVL774-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL774-.Ltext0
	.4byte	.LVL811-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST168:
	.4byte	.LVL770-.Ltext0
	.4byte	.LVL772-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL772-.Ltext0
	.4byte	.LVL773-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL774-.Ltext0
	.4byte	.LVL775-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL775-.Ltext0
	.4byte	.LVL778-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL782-.Ltext0
	.4byte	.LVL785-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL799-.Ltext0
	.4byte	.LVL800-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL809-.Ltext0
	.4byte	.LVL810-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST169:
	.4byte	.LVL779-.Ltext0
	.4byte	.LVL780-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL788-.Ltext0
	.4byte	.LVL789-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL792-.Ltext0
	.4byte	.LVL793-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST170:
	.4byte	.LVL777-.Ltext0
	.4byte	.LVL790-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL791-.Ltext0
	.4byte	.LVL811-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST171:
	.4byte	.LVL786-.Ltext0
	.4byte	.LVL787-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL799-.Ltext0
	.4byte	.LVL802-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST172:
	.4byte	.LVL771-.Ltext0
	.4byte	.LVL783-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL784-.Ltext0
	.4byte	.LVL806-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL809-.Ltext0
	.4byte	.LVL811-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST173:
	.4byte	.LVL794-.Ltext0
	.4byte	.LVL795-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL796-.Ltext0
	.4byte	.LVL801-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL804-.Ltext0
	.4byte	.LVL805-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST174:
	.4byte	.LVL776-.Ltext0
	.4byte	.LVL811-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST175:
	.4byte	.LVL797-.Ltext0
	.4byte	.LVL798-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL804-.Ltext0
	.4byte	.LVL805-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST250:
	.4byte	.LVL1231-.Ltext0
	.4byte	.LVL1232-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1235-.Ltext0
	.4byte	.LVL1236-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1237-.Ltext0
	.4byte	.LVL1238-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST251:
	.4byte	.LVL1234-.Ltext0
	.4byte	.LVL1235-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1240-.Ltext0
	.4byte	.LVL1244-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1245-.Ltext0
	.4byte	.LFE258-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST252:
	.4byte	.LVL1233-.Ltext0
	.4byte	.LVL1239-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1244-.Ltext0
	.4byte	.LVL1245-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1246-.Ltext0
	.4byte	.LVL1247-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST253:
	.4byte	.LVL1249-.Ltext0
	.4byte	.LVL1252-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1255-.Ltext0
	.4byte	.LFE258-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST76:
	.4byte	.LVL324-.Ltext0
	.4byte	.LVL325-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL325-.Ltext0
	.4byte	.LVL335-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST77:
	.4byte	.LVL324-.Ltext0
	.4byte	.LVL326-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL326-1-.Ltext0
	.4byte	.LVL335-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST78:
	.4byte	.LVL324-.Ltext0
	.4byte	.LVL326-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL326-1-.Ltext0
	.4byte	.LVL335-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST79:
	.4byte	.LVL327-.Ltext0
	.4byte	.LVL330-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL331-.Ltext0
	.4byte	.LVL334-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST80:
	.4byte	.LVL328-.Ltext0
	.4byte	.LVL329-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL331-.Ltext0
	.4byte	.LVL332-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL333-.Ltext0
	.4byte	.LFE257-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST75:
	.4byte	.LVL320-.Ltext0
	.4byte	.LVL321-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL322-.Ltext0
	.4byte	.LVL323-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST191:
	.4byte	.LVL885-.Ltext0
	.4byte	.LVL887-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL887-.Ltext0
	.4byte	.LVL889-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL889-.Ltext0
	.4byte	.LFE252-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST73:
	.4byte	.LVL310-.Ltext0
	.4byte	.LVL311-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL311-.Ltext0
	.4byte	.LFE250-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST74:
	.4byte	.LVL314-.Ltext0
	.4byte	.LVL315-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL317-.Ltext0
	.4byte	.LFE250-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST236:
	.4byte	.LVL1169-.Ltext0
	.4byte	.LVL1171-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1171-.Ltext0
	.4byte	.LFE249-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST183:
	.4byte	.LVL832-.Ltext0
	.4byte	.LVL834-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL834-.Ltext0
	.4byte	.LVL864-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST184:
	.4byte	.LVL833-.Ltext0
	.4byte	.LVL864-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST185:
	.4byte	.LVL833-.Ltext0
	.4byte	.LVL856-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL857-.Ltext0
	.4byte	.LVL864-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST186:
	.4byte	.LVL851-.Ltext0
	.4byte	.LVL852-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL853-.Ltext0
	.4byte	.LVL854-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL859-.Ltext0
	.4byte	.LVL860-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL861-.Ltext0
	.4byte	.LVL862-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST187:
	.4byte	.LVL835-.Ltext0
	.4byte	.LVL836-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL837-.Ltext0
	.4byte	.LVL838-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL843-.Ltext0
	.4byte	.LVL844-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST68:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL296-.Ltext0
	.4byte	.LVL302-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL304-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST69:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL295-.Ltext0
	.4byte	.LVL297-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL304-.Ltext0
	.4byte	.LVL305-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL306-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST70:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL294-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST71:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL298-.Ltext0
	.4byte	.LVL300-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL301-.Ltext0
	.4byte	.LVL304-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST63:
	.4byte	.LVL202-.Ltext0
	.4byte	.LVL203-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL204-.Ltext0
	.4byte	.LVL205-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST62:
	.4byte	.LVL197-.Ltext0
	.4byte	.LVL199-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL199-.Ltext0
	.4byte	.LFE243-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST148:
	.4byte	.LVL648-.Ltext0
	.4byte	.LVL651-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL656-.Ltext0
	.4byte	.LVL659-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST149:
	.4byte	.LVL652-.Ltext0
	.4byte	.LVL656-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL659-.Ltext0
	.4byte	.LFE240-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST150:
	.4byte	.LVL653-.Ltext0
	.4byte	.LVL656-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST234:
	.4byte	.LVL1159-.Ltext0
	.4byte	.LVL1161-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1164-.Ltext0
	.4byte	.LFE239-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST235:
	.4byte	.LVL1156-.Ltext0
	.4byte	.LVL1160-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1161-.Ltext0
	.4byte	.LVL1164-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST212:
	.4byte	.LVL1034-.Ltext0
	.4byte	.LVL1035-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1035-.Ltext0
	.4byte	.LFE238-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST164:
	.4byte	.LVL754-.Ltext0
	.4byte	.LVL766-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST165:
	.4byte	.LVL752-.Ltext0
	.4byte	.LVL761-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL762-.Ltext0
	.4byte	.LVL764-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST166:
	.4byte	.LVL751-.Ltext0
	.4byte	.LVL758-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL759-.Ltext0
	.4byte	.LVL766-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST61:
	.4byte	.LVL192-.Ltext0
	.4byte	.LVL193-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL193-.Ltext0
	.4byte	.LFE234-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST196:
	.4byte	.LVL906-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL912-.Ltext0
	.4byte	.LVL913-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL913-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST197:
	.4byte	.LVL907-.Ltext0
	.4byte	.LVL908-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL909-.Ltext0
	.4byte	.LVL914-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL925-.Ltext0
	.4byte	.LVL927-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL931-.Ltext0
	.4byte	.LVL932-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL934-.Ltext0
	.4byte	.LVL935-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL935-.Ltext0
	.4byte	.LVL936-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL949-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL949-.Ltext0
	.4byte	.LVL950-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL956-.Ltext0
	.4byte	.LVL957-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL973-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL974-.Ltext0
	.4byte	.LVL975-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL981-.Ltext0
	.4byte	.LVL982-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST198:
	.4byte	.LVL908-.Ltext0
	.4byte	.LVL910-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL927-.Ltext0
	.4byte	.LVL928-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL937-.Ltext0
	.4byte	.LVL938-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL951-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL952-.Ltext0
	.4byte	.LVL953-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL955-.Ltext0
	.4byte	.LVL957-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL973-.Ltext0
	.4byte	.LVL975-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL981-.Ltext0
	.4byte	.LVL982-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST199:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL960-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL963-.Ltext0
	.4byte	.LVL965-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL970-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL973-.Ltext0
	.4byte	.LVL980-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL981-.Ltext0
	.4byte	.LVL985-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL988-.Ltext0
	.4byte	.LVL990-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL991-.Ltext0
	.4byte	.LVL993-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1024-.Ltext0
	.4byte	.LVL1025-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1031-.Ltext0
	.4byte	.LVL1033-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST200:
	.4byte	.LVL926-.Ltext0
	.4byte	.LVL930-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL946-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL976-.Ltext0
	.4byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1031-.Ltext0
	.4byte	.LVL1032-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST201:
	.4byte	.LVL921-.Ltext0
	.4byte	.LVL922-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL923-.Ltext0
	.4byte	.LVL924-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL924-.Ltext0
	.4byte	.LVL926-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL943-.Ltext0
	.4byte	.LVL944-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL946-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL976-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL977-.Ltext0
	.4byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST202:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL915-.Ltext0
	.4byte	.LVL916-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL916-.Ltext0
	.4byte	.LVL931-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL933-.Ltext0
	.4byte	.LVL947-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL956-.Ltext0
	.4byte	.LVL959-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST203:
	.4byte	.LVL920-.Ltext0
	.4byte	.LVL922-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL939-.Ltext0
	.4byte	.LVL940-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL942-.Ltext0
	.4byte	.LVL943-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST204:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL959-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL963-.Ltext0
	.4byte	.LVL966-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL969-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL979-.Ltext0
	.4byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL992-.Ltext0
	.4byte	.LVL1001-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1003-.Ltext0
	.4byte	.LVL1022-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1033-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST205:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL931-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL1033-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1033-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST206:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL931-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL958-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	0
	.4byte	0
.LLST207:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL931-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL1009-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1009-.Ltext0
	.4byte	.LVL1010-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1010-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST208:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL959-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL963-.Ltext0
	.4byte	.LVL966-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL969-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1001-.Ltext0
	.4byte	.LVL1003-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1022-.Ltext0
	.4byte	.LVL1024-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST209:
	.4byte	.LVL918-.Ltext0
	.4byte	.LVL919-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL948-.Ltext0
	.4byte	.LVL949-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL961-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL963-.Ltext0
	.4byte	.LVL964-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL971-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL981-.Ltext0
	.4byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL984-.Ltext0
	.4byte	.LVL987-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL988-.Ltext0
	.4byte	.LVL989-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1027-.Ltext0
	.4byte	.LVL1028-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1029-.Ltext0
	.4byte	.LVL1030-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1030-.Ltext0
	.4byte	.LVL1031-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST210:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL968-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL997-.Ltext0
	.4byte	.LVL1003-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1005-.Ltext0
	.4byte	.LVL1016-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1022-.Ltext0
	.4byte	.LVL1024-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST211:
	.4byte	.LVL997-.Ltext0
	.4byte	.LVL998-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1007-.Ltext0
	.4byte	.LVL1008-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1012-.Ltext0
	.4byte	.LVL1015-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST141:
	.4byte	.LVL615-.Ltext0
	.4byte	.LVL616-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL616-.Ltext0
	.4byte	.LVL618-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL618-.Ltext0
	.4byte	.LVL632-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST142:
	.4byte	.LVL615-.Ltext0
	.4byte	.LVL616-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL617-.Ltext0
	.4byte	.LVL619-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL619-1-.Ltext0
	.4byte	.LFE230-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -28
	.4byte	0
	.4byte	0
.LLST143:
	.4byte	.LVL620-.Ltext0
	.4byte	.LVL632-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST144:
	.4byte	.LVL622-.Ltext0
	.4byte	.LVL628-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL629-.Ltext0
	.4byte	.LVL632-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST145:
	.4byte	.LVL621-.Ltext0
	.4byte	.LVL623-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL627-.Ltext0
	.4byte	.LVL630-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST59:
	.4byte	.LVL183-.Ltext0
	.4byte	.LVL184-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL186-.Ltext0
	.4byte	.LVL189-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST60:
	.4byte	.LVL183-.Ltext0
	.4byte	.LVL185-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL186-.Ltext0
	.4byte	.LVL187-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL188-.Ltext0
	.4byte	.LVL189-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST157:
	.4byte	.LVL697-.Ltext0
	.4byte	.LVL703-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL703-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST158:
	.4byte	.LVL708-.Ltext0
	.4byte	.LVL712-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL718-.Ltext0
	.4byte	.LVL719-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL726-.Ltext0
	.4byte	.LVL729-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST159:
	.4byte	.LVL714-.Ltext0
	.4byte	.LVL715-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST160:
	.4byte	.LVL698-.Ltext0
	.4byte	.LVL700-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL706-.Ltext0
	.4byte	.LVL709-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL709-.Ltext0
	.4byte	.LVL710-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL717-.Ltext0
	.4byte	.LVL719-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL719-1-.Ltext0
	.4byte	.LVL723-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL723-.Ltext0
	.4byte	.LVL724-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL725-.Ltext0
	.4byte	.LVL726-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL726-.Ltext0
	.4byte	.LVL728-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL731-.Ltext0
	.4byte	.LVL732-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL734-.Ltext0
	.4byte	.LVL735-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL736-.Ltext0
	.4byte	.LVL737-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL738-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST161:
	.4byte	.LVL701-.Ltext0
	.4byte	.LVL714-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL718-.Ltext0
	.4byte	.LVL723-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL726-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST162:
	.4byte	.LVL700-.Ltext0
	.4byte	.LVL702-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL713-.Ltext0
	.4byte	.LVL716-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST163:
	.4byte	.LVL702-.Ltext0
	.4byte	.LVL704-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL705-.Ltext0
	.4byte	.LVL706-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL707-.Ltext0
	.4byte	.LVL711-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL718-.Ltext0
	.4byte	.LVL719-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL726-.Ltext0
	.4byte	.LVL727-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST296:
	.4byte	.LVL1545-.Ltext0
	.4byte	.LVL1546-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1548-.Ltext0
	.4byte	.LVL1554-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1561-.Ltext0
	.4byte	.LVL1563-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1564-.Ltext0
	.4byte	.LFE224-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST58:
	.4byte	.LVL181-.Ltext0
	.4byte	.LVL182-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST154:
	.4byte	.LVL675-.Ltext0
	.4byte	.LVL683-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL684-.Ltext0
	.4byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST155:
	.4byte	.LVL676-.Ltext0
	.4byte	.LVL680-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL681-.Ltext0
	.4byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST156:
	.4byte	.LVL676-.Ltext0
	.4byte	.LVL677-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL682-.Ltext0
	.4byte	.LVL685-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL686-.Ltext0
	.4byte	.LVL694-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL695-.Ltext0
	.4byte	.LVL696-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST232:
	.4byte	.LVL1141-.Ltext0
	.4byte	.LVL1155-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST233:
	.4byte	.LVL1146-.Ltext0
	.4byte	.LVL1151-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1152-.Ltext0
	.4byte	.LVL1155-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST151:
	.4byte	.LVL661-.Ltext0
	.4byte	.LVL666-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL667-.Ltext0
	.4byte	.LVL670-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL670-.Ltext0
	.4byte	.LFE217-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST152:
	.4byte	.LVL663-.Ltext0
	.4byte	.LVL664-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL667-.Ltext0
	.4byte	.LVL668-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST153:
	.4byte	.LVL662-.Ltext0
	.4byte	.LVL663-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST224:
	.4byte	.LVL1108-.Ltext0
	.4byte	.LVL1112-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1114-.Ltext0
	.4byte	.LVL1116-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST225:
	.4byte	.LVL1109-.Ltext0
	.4byte	.LVL1110-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1111-.Ltext0
	.4byte	.LVL1113-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1114-.Ltext0
	.4byte	.LVL1115-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST226:
	.4byte	.LVL1119-.Ltext0
	.4byte	.LVL1123-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1123-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST227:
	.4byte	.LVL1119-.Ltext0
	.4byte	.LVL1123-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1123-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST228:
	.4byte	.LVL1119-.Ltext0
	.4byte	.LVL1120-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1120-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST229:
	.4byte	.LVL1122-.Ltext0
	.4byte	.LVL1124-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1128-.Ltext0
	.4byte	.LVL1134-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST230:
	.4byte	.LVL1125-.Ltext0
	.4byte	.LVL1126-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1127-.Ltext0
	.4byte	.LVL1129-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1132-.Ltext0
	.4byte	.LVL1133-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST231:
	.4byte	.LVL1121-.Ltext0
	.4byte	.LVL1122-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST218:
	.4byte	.LVL1089-.Ltext0
	.4byte	.LVL1093-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1093-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST219:
	.4byte	.LVL1089-.Ltext0
	.4byte	.LVL1093-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1093-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST220:
	.4byte	.LVL1089-.Ltext0
	.4byte	.LVL1090-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1090-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST221:
	.4byte	.LVL1092-.Ltext0
	.4byte	.LVL1097-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1098-.Ltext0
	.4byte	.LVL1106-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST222:
	.4byte	.LVL1094-.Ltext0
	.4byte	.LVL1095-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1096-.Ltext0
	.4byte	.LVL1099-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1103-.Ltext0
	.4byte	.LVL1104-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST223:
	.4byte	.LVL1091-.Ltext0
	.4byte	.LVL1106-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST137:
	.4byte	.LVL602-.Ltext0
	.4byte	.LVL605-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL606-.Ltext0
	.4byte	.LVL608-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL611-.Ltext0
	.4byte	.LVL612-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST138:
	.4byte	.LVL602-.Ltext0
	.4byte	.LVL606-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL606-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST139:
	.4byte	.LVL602-.Ltext0
	.4byte	.LVL606-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL606-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST140:
	.4byte	.LVL607-.Ltext0
	.4byte	.LVL613-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL614-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST195:
	.4byte	.LVL902-.Ltext0
	.4byte	.LVL903-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL904-.Ltext0
	.4byte	.LVL905-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL905-.Ltext0
	.4byte	.LFE211-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST136:
	.4byte	.LVL599-.Ltext0
	.4byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST129:
	.4byte	.LVL570-.Ltext0
	.4byte	.LVL572-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL572-.Ltext0
	.4byte	.LVL583-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST130:
	.4byte	.LVL570-.Ltext0
	.4byte	.LVL572-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL572-.Ltext0
	.4byte	.LVL583-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST131:
	.4byte	.LVL570-.Ltext0
	.4byte	.LVL583-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST132:
	.4byte	.LVL576-.Ltext0
	.4byte	.LVL577-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST133:
	.4byte	.LVL571-.Ltext0
	.4byte	.LVL579-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL580-.Ltext0
	.4byte	.LVL583-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST125:
	.4byte	.LVL553-.Ltext0
	.4byte	.LVL555-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL555-.Ltext0
	.4byte	.LVL558-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL558-.Ltext0
	.4byte	.LVL561-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL561-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST126:
	.4byte	.LVL563-.Ltext0
	.4byte	.LVL564-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL567-.Ltext0
	.4byte	.LVL568-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL569-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST127:
	.4byte	.LVL554-.Ltext0
	.4byte	.LVL559-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST128:
	.4byte	.LVL557-.Ltext0
	.4byte	.LVL558-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL560-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST134:
	.4byte	.LVL584-.Ltext0
	.4byte	.LVL585-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL585-.Ltext0
	.4byte	.LVL588-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL588-1-.Ltext0
	.4byte	.LFE206-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST135:
	.4byte	.LVL586-.Ltext0
	.4byte	.LVL591-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL593-.Ltext0
	.4byte	.LVL595-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST120:
	.4byte	.LVL537-.Ltext0
	.4byte	.LVL538-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL538-.Ltext0
	.4byte	.LVL542-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL542-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST121:
	.4byte	.LVL541-.Ltext0
	.4byte	.LVL547-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL549-.Ltext0
	.4byte	.LVL551-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST122:
	.4byte	.LVL544-.Ltext0
	.4byte	.LVL545-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL550-.Ltext0
	.4byte	.LVL551-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST123:
	.4byte	.LVL546-.Ltext0
	.4byte	.LVL549-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL551-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST124:
	.4byte	.LVL548-.Ltext0
	.4byte	.LVL549-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL551-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST117:
	.4byte	.LVL525-.Ltext0
	.4byte	.LVL526-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL526-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST118:
	.4byte	.LVL525-.Ltext0
	.4byte	.LVL526-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL526-.Ltext0
	.4byte	.LVL532-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST119:
	.4byte	.LVL529-.Ltext0
	.4byte	.LVL530-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL532-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST47:
	.4byte	.LVL149-.Ltext0
	.4byte	.LVL153-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL154-.Ltext0
	.4byte	.LVL155-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL157-.Ltext0
	.4byte	.LVL158-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST48:
	.4byte	.LVL150-.Ltext0
	.4byte	.LVL151-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL152-.Ltext0
	.4byte	.LVL154-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL156-.Ltext0
	.4byte	.LVL159-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST46:
	.4byte	.LVL147-.Ltext0
	.4byte	.LVL148-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST43:
	.4byte	.LVL138-.Ltext0
	.4byte	.LVL142-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL143-.Ltext0
	.4byte	.LVL146-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST44:
	.4byte	.LVL138-.Ltext0
	.4byte	.LVL141-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL143-.Ltext0
	.4byte	.LVL144-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL145-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST45:
	.4byte	.LVL139-.Ltext0
	.4byte	.LVL140-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL143-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST41:
	.4byte	.LVL131-.Ltext0
	.4byte	.LVL134-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL134-.Ltext0
	.4byte	.LFE198-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST42:
	.4byte	.LVL131-.Ltext0
	.4byte	.LVL133-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL134-.Ltext0
	.4byte	.LVL135-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL136-.Ltext0
	.4byte	.LVL137-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST38:
	.4byte	.LVL121-.Ltext0
	.4byte	.LVL125-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL126-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST39:
	.4byte	.LVL124-.Ltext0
	.4byte	.LVL126-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL127-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST30:
	.4byte	.LVL94-.Ltext0
	.4byte	.LVL95-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL95-.Ltext0
	.4byte	.LVL99-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL100-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST31:
	.4byte	.LVL96-.Ltext0
	.4byte	.LVL108-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL109-.Ltext0
	.4byte	.LVL110-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL111-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST32:
	.4byte	.LVL97-.Ltext0
	.4byte	.LVL116-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL117-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST33:
	.4byte	.LVL101-.Ltext0
	.4byte	.LVL103-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL111-.Ltext0
	.4byte	.LVL113-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST34:
	.4byte	.LVL98-.Ltext0
	.4byte	.LVL100-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL104-.Ltext0
	.4byte	.LVL111-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL113-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST35:
	.4byte	.LVL98-.Ltext0
	.4byte	.LVL100-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL102-.Ltext0
	.4byte	.LVL111-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL112-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST36:
	.4byte	.LVL98-.Ltext0
	.4byte	.LVL100-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL105-.Ltext0
	.4byte	.LVL106-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL107-.Ltext0
	.4byte	.LVL111-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL114-.Ltext0
	.4byte	.LVL115-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST29:
	.4byte	.LVL89-.Ltext0
	.4byte	.LVL91-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL92-.Ltext0
	.4byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST26:
	.4byte	.LVL77-.Ltext0
	.4byte	.LVL78-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL83-.Ltext0
	.4byte	.LVL84-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL86-.Ltext0
	.4byte	.LFE191-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST27:
	.4byte	.LVL77-.Ltext0
	.4byte	.LVL79-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL81-.Ltext0
	.4byte	.LVL82-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL85-.Ltext0
	.4byte	.LVL87-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST28:
	.4byte	.LVL77-.Ltext0
	.4byte	.LVL79-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL81-.Ltext0
	.4byte	.LVL88-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST98:
	.4byte	.LVL410-.Ltext0
	.4byte	.LVL413-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL413-.Ltext0
	.4byte	.LVL414-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL414-.Ltext0
	.4byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST99:
	.4byte	.LVL410-.Ltext0
	.4byte	.LVL411-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL413-.Ltext0
	.4byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST25:
	.4byte	.LVL74-.Ltext0
	.4byte	.LVL75-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST88:
	.4byte	.LVL370-.Ltext0
	.4byte	.LVL372-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST89:
	.4byte	.LVL370-.Ltext0
	.4byte	.LVL371-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL371-.Ltext0
	.4byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST24:
	.4byte	.LVL70-.Ltext0
	.4byte	.LVL71-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL72-.Ltext0
	.4byte	.LVL73-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST23:
	.4byte	.LVL67-.Ltext0
	.4byte	.LVL68-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST22:
	.4byte	.LVL63-.Ltext0
	.4byte	.LVL64-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL64-1-.Ltext0
	.4byte	.LVL66-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST20:
	.4byte	.LVL57-.Ltext0
	.4byte	.LVL59-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL59-1-.Ltext0
	.4byte	.LVL62-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST21:
	.4byte	.LVL57-.Ltext0
	.4byte	.LVL58-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL58-.Ltext0
	.4byte	.LVL60-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST216:
	.4byte	.LVL1060-.Ltext0
	.4byte	.LVL1088-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST217:
	.4byte	.LVL1062-.Ltext0
	.4byte	.LVL1066-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1067-.Ltext0
	.4byte	.LVL1076-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1077-.Ltext0
	.4byte	.LVL1088-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST86:
	.4byte	.LVL362-.Ltext0
	.4byte	.LVL364-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL365-.Ltext0
	.4byte	.LVL366-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL367-.Ltext0
	.4byte	.LFE180-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST87:
	.4byte	.LVL368-.Ltext0
	.4byte	.LVL369-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST213:
	.4byte	.LVL1048-.Ltext0
	.4byte	.LVL1049-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1056-.Ltext0
	.4byte	.LVL1058-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST214:
	.4byte	.LVL1040-.Ltext0
	.4byte	.LVL1045-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1049-.Ltext0
	.4byte	.LVL1050-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1051-.Ltext0
	.4byte	.LVL1052-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1058-.Ltext0
	.4byte	.LVL1059-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST215:
	.4byte	.LVL1045-.Ltext0
	.4byte	.LVL1048-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1052-.Ltext0
	.4byte	.LVL1054-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1055-.Ltext0
	.4byte	.LVL1056-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST19:
	.4byte	.LVL54-.Ltext0
	.4byte	.LVL55-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST179:
	.4byte	.LVL814-.Ltext0
	.4byte	.LVL817-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL828-.Ltext0
	.4byte	.LVL830-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST180:
	.4byte	.LVL819-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL830-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST181:
	.4byte	.LVL816-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL830-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST182:
	.4byte	.LVL817-.Ltext0
	.4byte	.LVL824-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL825-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL830-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST115:
	.4byte	.LVL519-.Ltext0
	.4byte	.LVL521-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST116:
	.4byte	.LVL519-.Ltext0
	.4byte	.LVL520-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL520-.Ltext0
	.4byte	.LVL524-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST16:
	.4byte	.LVL45-.Ltext0
	.4byte	.LVL46-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL46-1-.Ltext0
	.4byte	.LVL52-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST17:
	.4byte	.LVL49-.Ltext0
	.4byte	.LVL51-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST18:
	.4byte	.LVL47-.Ltext0
	.4byte	.LVL50-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST14:
	.4byte	.LVL38-.Ltext0
	.4byte	.LVL39-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL39-.Ltext0
	.4byte	.LVL40-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL40-1-.Ltext0
	.4byte	.LVL44-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST15:
	.4byte	.LVL41-.Ltext0
	.4byte	.LVL44-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST293:
	.4byte	.LVL1539-.Ltext0
	.4byte	.LVL1542-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST294:
	.4byte	.LVL1539-.Ltext0
	.4byte	.LVL1541-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST295:
	.4byte	.LVL1539-.Ltext0
	.4byte	.LVL1540-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST279:
	.4byte	.LVL1438-.Ltext0
	.4byte	.LVL1441-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST280:
	.4byte	.LVL1438-.Ltext0
	.4byte	.LVL1440-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST281:
	.4byte	.LVL1438-.Ltext0
	.4byte	.LVL1439-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST244:
	.4byte	.LVL1211-.Ltext0
	.4byte	.LVL1212-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1212-.Ltext0
	.4byte	.LVL1214-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1214-.Ltext0
	.4byte	.LVL1218-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1230-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST245:
	.4byte	.LVL1211-.Ltext0
	.4byte	.LVL1212-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1212-.Ltext0
	.4byte	.LVL1215-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1215-1-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST246:
	.4byte	.LVL1213-.Ltext0
	.4byte	.LVL1214-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1219-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST247:
	.4byte	.LVL1213-.Ltext0
	.4byte	.LVL1214-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1217-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST248:
	.4byte	.LVL1213-.Ltext0
	.4byte	.LVL1214-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	.LVL1220-.Ltext0
	.4byte	.LVL1224-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	.LVL1224-.Ltext0
	.4byte	.LVL1225-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1226-.Ltext0
	.4byte	.LVL1228-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	.LVL1228-.Ltext0
	.4byte	.LVL1229-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1229-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	0
	.4byte	0
.LLST249:
	.4byte	.LVL1213-.Ltext0
	.4byte	.LVL1214-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -28
	.4byte	.LVL1220-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -28
	.4byte	0
	.4byte	0
.LLST282:
	.4byte	.LVL1443-.Ltext0
	.4byte	.LVL1445-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1447-.Ltext0
	.4byte	.LVL1450-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1537-.Ltext0
	.4byte	.LVL1538-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST283:
	.4byte	.LVL1443-.Ltext0
	.4byte	.LVL1446-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1537-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST284:
	.4byte	.LVL1443-.Ltext0
	.4byte	.LVL1444-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1447-.Ltext0
	.4byte	.LVL1448-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1448-.Ltext0
	.4byte	.LVL1449-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1485-.Ltext0
	.4byte	.LVL1486-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1490-.Ltext0
	.4byte	.LVL1491-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1503-.Ltext0
	.4byte	.LVL1504-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1513-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1514-.Ltext0
	.4byte	.LVL1515-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1518-.Ltext0
	.4byte	.LVL1519-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST285:
	.4byte	.LVL1443-.Ltext0
	.4byte	.LVL1446-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1447-.Ltext0
	.4byte	.LVL1448-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1491-.Ltext0
	.4byte	.LVL1492-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1507-.Ltext0
	.4byte	.LVL1508-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1508-.Ltext0
	.4byte	.LVL1509-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1510-.Ltext0
	.4byte	.LVL1512-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1527-.Ltext0
	.4byte	.LVL1528-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1531-.Ltext0
	.4byte	.LVL1532-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST286:
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1460-.Ltext0
	.4byte	.LVL1483-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1488-.Ltext0
	.4byte	.LVL1489-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1495-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST287:
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1452-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST288:
	.4byte	.LVL1453-.Ltext0
	.4byte	.LVL1456-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1476-.Ltext0
	.4byte	.LVL1477-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1502-.Ltext0
	.4byte	.LVL1503-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1511-.Ltext0
	.4byte	.LVL1512-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1522-.Ltext0
	.4byte	.LVL1523-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1529-.Ltext0
	.4byte	.LVL1530-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST289:
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1460-.Ltext0
	.4byte	.LVL1483-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1488-.Ltext0
	.4byte	.LVL1489-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1495-.Ltext0
	.4byte	.LVL1516-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1516-.Ltext0
	.4byte	.LVL1517-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1517-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	0
	.4byte	0
.LLST290:
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1460-.Ltext0
	.4byte	.LVL1464-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1474-.Ltext0
	.4byte	.LVL1483-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1488-.Ltext0
	.4byte	.LVL1489-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1497-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST291:
	.4byte	.LVL1454-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1455-.Ltext0
	.4byte	.LVL1457-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1457-.Ltext0
	.4byte	.LVL1458-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1459-.Ltext0
	.4byte	.LVL1461-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1461-.Ltext0
	.4byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1471-.Ltext0
	.4byte	.LVL1472-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1473-.Ltext0
	.4byte	.LVL1475-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1475-.Ltext0
	.4byte	.LVL1476-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1483-.Ltext0
	.4byte	.LVL1484-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1493-.Ltext0
	.4byte	.LVL1495-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1497-.Ltext0
	.4byte	.LVL1498-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1532-.Ltext0
	.4byte	.LVL1533-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1535-.Ltext0
	.4byte	.LVL1536-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1536-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST292:
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1459-.Ltext0
	.4byte	.LVL1467-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1469-.Ltext0
	.4byte	.LVL1478-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1488-.Ltext0
	.4byte	.LVL1489-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1494-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST237:
	.4byte	.LVL1180-.Ltext0
	.4byte	.LVL1184-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1184-1-.Ltext0
	.4byte	.LVL1185-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1190-.Ltext0
	.4byte	.LVL1191-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1201-.Ltext0
	.4byte	.LVL1204-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST238:
	.4byte	.LVL1180-.Ltext0
	.4byte	.LVL1184-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1184-1-.Ltext0
	.4byte	.LVL1204-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST239:
	.4byte	.LVL1180-.Ltext0
	.4byte	.LVL1182-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST240:
	.4byte	.LVL1180-.Ltext0
	.4byte	.LVL1183-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1183-.Ltext0
	.4byte	.LVL1204-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST241:
	.4byte	.LVL1181-.Ltext0
	.4byte	.LVL1204-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST242:
	.4byte	.LVL1184-.Ltext0
	.4byte	.LVL1185-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	.LVL1185-.Ltext0
	.4byte	.LVL1193-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1193-.Ltext0
	.4byte	.LFE165-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	0
	.4byte	0
.LLST243:
	.4byte	.LVL1184-.Ltext0
	.4byte	.LVL1185-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1190-.Ltext0
	.4byte	.LVL1191-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1195-.Ltext0
	.4byte	.LVL1196-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1197-.Ltext0
	.4byte	.LVL1198-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1200-.Ltext0
	.4byte	.LVL1202-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST264:
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1373-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1377-.Ltext0
	.4byte	.LVL1380-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1437-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST265:
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1375-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1437-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST266:
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1372-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1377-.Ltext0
	.4byte	.LVL1378-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1378-.Ltext0
	.4byte	.LVL1379-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1426-.Ltext0
	.4byte	.LVL1427-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST267:
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1374-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1374-1-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST268:
	.4byte	.LVL1385-.Ltext0
	.4byte	.LVL1387-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1408-.Ltext0
	.4byte	.LVL1409-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1412-.Ltext0
	.4byte	.LVL1413-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1422-.Ltext0
	.4byte	.LVL1423-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1428-.Ltext0
	.4byte	.LVL1429-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1433-.Ltext0
	.4byte	.LVL1434-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST269:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1388-.Ltext0
	.4byte	.LVL1394-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1396-.Ltext0
	.4byte	.LVL1397-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1410-.Ltext0
	.4byte	.LVL1411-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1412-.Ltext0
	.4byte	.LVL1413-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1429-.Ltext0
	.4byte	.LVL1430-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1431-.Ltext0
	.4byte	.LVL1432-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1434-.Ltext0
	.4byte	.LVL1435-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1436-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST270:
	.4byte	.LVL1399-.Ltext0
	.4byte	.LVL1400-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1417-.Ltext0
	.4byte	.LVL1418-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1419-.Ltext0
	.4byte	.LVL1420-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST271:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1386-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST272:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST273:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1399-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1404-.Ltext0
	.4byte	.LVL1416-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1420-.Ltext0
	.4byte	.LVL1421-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1433-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST274:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1382-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST275:
	.4byte	.LVL1389-.Ltext0
	.4byte	.LVL1390-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1390-.Ltext0
	.4byte	.LVL1392-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1392-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1396-.Ltext0
	.4byte	.LVL1398-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1401-.Ltext0
	.4byte	.LVL1402-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1403-.Ltext0
	.4byte	.LVL1405-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST276:
	.4byte	.LVL1406-.Ltext0
	.4byte	.LVL1407-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1424-.Ltext0
	.4byte	.LVL1425-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST277:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1381-.Ltext0
	.4byte	.LVL1437-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST278:
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1404-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1416-.Ltext0
	.4byte	.LVL1421-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST298:
	.4byte	.LVL1596-.Ltext0
	.4byte	.LVL1599-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1613-.Ltext0
	.4byte	.LVL1615-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1616-.Ltext0
	.4byte	.LVL1617-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST299:
	.4byte	.LVL1594-.Ltext0
	.4byte	.LVL1595-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1604-.Ltext0
	.4byte	.LVL1606-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1607-.Ltext0
	.4byte	.LVL1611-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST100:
	.4byte	.LVL415-.Ltext0
	.4byte	.LVL417-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL432-.Ltext0
	.4byte	.LVL433-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL433-.Ltext0
	.4byte	.LVL434-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL434-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL435-.Ltext0
	.4byte	.LVL436-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST101:
	.4byte	.LVL415-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL435-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST102:
	.4byte	.LVL416-.Ltext0
	.4byte	.LVL440-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL441-.Ltext0
	.4byte	.LVL442-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL443-.Ltext0
	.4byte	.LVL452-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL453-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST103:
	.4byte	.LVL419-.Ltext0
	.4byte	.LVL421-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL422-.Ltext0
	.4byte	.LVL424-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL428-.Ltext0
	.4byte	.LVL431-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL447-.Ltext0
	.4byte	.LVL448-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST104:
	.4byte	.LVL423-.Ltext0
	.4byte	.LVL427-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL429-.Ltext0
	.4byte	.LVL430-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL433-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL448-.Ltext0
	.4byte	.LVL455-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST105:
	.4byte	.LVL421-.Ltext0
	.4byte	.LVL424-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL426-.Ltext0
	.4byte	.LVL428-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL445-.Ltext0
	.4byte	.LVL447-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST106:
	.4byte	.LVL420-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL446-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST114:
	.4byte	.LVL514-.Ltext0
	.4byte	.LVL515-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL517-.Ltext0
	.4byte	.LVL518-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST11:
	.4byte	.LVL24-.Ltext0
	.4byte	.LVL25-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL25-.Ltext0
	.4byte	.LVL33-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST12:
	.4byte	.LVL26-.Ltext0
	.4byte	.LVL27-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST10:
	.4byte	.LVL22-.Ltext0
	.4byte	.LVL23-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST9:
	.4byte	.LVL17-.Ltext0
	.4byte	.LVL19-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL20-.Ltext0
	.4byte	.LFE152-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST67:
	.4byte	.LVL285-.Ltext0
	.4byte	.LVL286-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL286-1-.Ltext0
	.4byte	.LVL288-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST64:
	.4byte	.LVL206-.Ltext0
	.4byte	.LVL207-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL207-1-.Ltext0
	.4byte	.LFE150-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST65:
	.4byte	.LVL278-.Ltext0
	.4byte	.LVL279-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL280-.Ltext0
	.4byte	.LVL281-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST66:
	.4byte	.LVL274-.Ltext0
	.4byte	.LVL275-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL282-.Ltext0
	.4byte	.LVL283-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL284-.Ltext0
	.4byte	.LFE150-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST8:
	.4byte	.LVL15-.Ltext0
	.4byte	.LVL16-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST13:
	.4byte	.LVL34-.Ltext0
	.4byte	.LVL35-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL36-.Ltext0
	.4byte	.LVL37-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST37:
	.4byte	.LVL118-.Ltext0
	.4byte	.LVL119-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST40:
	.4byte	.LVL128-.Ltext0
	.4byte	.LVL129-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST49:
	.4byte	.LVL160-.Ltext0
	.4byte	.LVL167-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL167-.Ltext0
	.4byte	.LVL168-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST50:
	.4byte	.LVL162-.Ltext0
	.4byte	.LVL168-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST51:
	.4byte	.LVL164-.Ltext0
	.4byte	.LVL165-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL167-.Ltext0
	.4byte	.LVL168-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST52:
	.4byte	.LVL161-.Ltext0
	.4byte	.LVL168-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST53:
	.4byte	.LVL169-.Ltext0
	.4byte	.LVL172-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL172-1-.Ltext0
	.4byte	.LVL173-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST54:
	.4byte	.LVL170-.Ltext0
	.4byte	.LVL173-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST55:
	.4byte	.LVL175-.Ltext0
	.4byte	.LVL176-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST56:
	.4byte	.LVL177-.Ltext0
	.4byte	.LVL178-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL178-.Ltext0
	.4byte	.LVL179-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST57:
	.4byte	.LVL177-.Ltext0
	.4byte	.LVL178-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL178-.Ltext0
	.4byte	.LVL180-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST72:
	.4byte	.LVL307-.Ltext0
	.4byte	.LVL308-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL308-.Ltext0
	.4byte	.LVL309-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST107:
	.4byte	.LVL457-.Ltext0
	.4byte	.LVL458-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL462-.Ltext0
	.4byte	.LVL463-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL464-.Ltext0
	.4byte	.LVL465-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL465-.Ltext0
	.4byte	.LFE275-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST111:
	.4byte	.LVL477-.Ltext0
	.4byte	.LVL478-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL478-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST112:
	.4byte	.LVL477-.Ltext0
	.4byte	.LVL478-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL478-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST113:
	.4byte	.LVL477-.Ltext0
	.4byte	.LVL478-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL478-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST146:
	.4byte	.LVL640-.Ltext0
	.4byte	.LVL645-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -104
	.4byte	0
	.4byte	0
.LLST147:
	.4byte	.LVL646-.Ltext0
	.4byte	.LVL647-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST188:
	.4byte	.LVL865-.Ltext0
	.4byte	.LVL867-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL867-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST189:
	.4byte	.LVL866-.Ltext0
	.4byte	.LVL873-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL874-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST190:
	.4byte	.LVL875-.Ltext0
	.4byte	.LVL876-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL876-.Ltext0
	.4byte	.LVL882-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL882-.Ltext0
	.4byte	.LVL884-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL884-1-.Ltext0
	.4byte	.LFE251-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST192:
	.4byte	.LVL891-.Ltext0
	.4byte	.LVL901-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST193:
	.4byte	.LVL892-.Ltext0
	.4byte	.LVL901-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	0
	.4byte	0
.LLST194:
	.4byte	.LVL894-.Ltext0
	.4byte	.LVL895-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL897-.Ltext0
	.4byte	.LVL901-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST255:
	.4byte	.LVL1286-.Ltext0
	.4byte	.LVL1287-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1287-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1308-.Ltext0
	.4byte	.LVL1309-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1313-.Ltext0
	.4byte	.LVL1314-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1318-.Ltext0
	.4byte	.LVL1320-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1366-.Ltext0
	.4byte	.LFE269-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST256:
	.4byte	.LVL1286-.Ltext0
	.4byte	.LVL1288-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1296-.Ltext0
	.4byte	.LVL1297-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1366-.Ltext0
	.4byte	.LVL1367-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST257:
	.4byte	.LVL1295-.Ltext0
	.4byte	.LVL1297-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1298-.Ltext0
	.4byte	.LVL1299-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1300-.Ltext0
	.4byte	.LVL1301-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1302-.Ltext0
	.4byte	.LVL1303-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1318-.Ltext0
	.4byte	.LVL1332-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1333-.Ltext0
	.4byte	.LVL1354-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1364-.Ltext0
	.4byte	.LVL1365-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1368-.Ltext0
	.4byte	.LVL1369-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST258:
	.4byte	.LVL1344-.Ltext0
	.4byte	.LVL1345-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1346-.Ltext0
	.4byte	.LVL1347-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST259:
	.4byte	.LVL1296-.Ltext0
	.4byte	.LVL1297-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1309-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1319-.Ltext0
	.4byte	.LVL1349-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1349-.Ltext0
	.4byte	.LVL1350-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1350-.Ltext0
	.4byte	.LVL1366-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST260:
	.4byte	.LVL1296-.Ltext0
	.4byte	.LVL1297-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1309-.Ltext0
	.4byte	.LVL1311-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1327-.Ltext0
	.4byte	.LVL1337-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1339-.Ltext0
	.4byte	.LVL1366-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST261:
	.4byte	.LVL1336-.Ltext0
	.4byte	.LVL1338-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1340-.Ltext0
	.4byte	.LVL1342-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST262:
	.4byte	.LVL1329-.Ltext0
	.4byte	.LVL1330-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1331-.Ltext0
	.4byte	.LVL1335-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1341-.Ltext0
	.4byte	.LVL1342-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1353-.Ltext0
	.4byte	.LVL1355-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1355-.Ltext0
	.4byte	.LVL1356-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1357-.Ltext0
	.4byte	.LVL1358-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST263:
	.4byte	.LVL1296-.Ltext0
	.4byte	.LVL1297-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1309-.Ltext0
	.4byte	.LVL1310-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1327-.Ltext0
	.4byte	.LVL1334-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1343-.Ltext0
	.4byte	.LVL1366-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST297:
	.4byte	.LVL1576-.Ltext0
	.4byte	.LVL1577-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1584-.Ltext0
	.4byte	.LVL1585-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
	.section	.debug_aranges,"",%progbits
	.4byte	0x1c
	.2byte	0x2
	.4byte	.Ldebug_info0
	.byte	0x4
	.byte	0
	.2byte	0
	.2byte	0
	.4byte	.Ltext0
	.4byte	.Letext0-.Ltext0
	.4byte	0
	.4byte	0
	.section	.debug_ranges,"",%progbits
.Ldebug_ranges0:
	.4byte	.LBB218-.Ltext0
	.4byte	.LBE218-.Ltext0
	.4byte	.LBB221-.Ltext0
	.4byte	.LBE221-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB227-.Ltext0
	.4byte	.LBE227-.Ltext0
	.4byte	.LBB232-.Ltext0
	.4byte	.LBE232-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB238-.Ltext0
	.4byte	.LBE238-.Ltext0
	.4byte	.LBB245-.Ltext0
	.4byte	.LBE245-.Ltext0
	.4byte	.LBB246-.Ltext0
	.4byte	.LBE246-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB247-.Ltext0
	.4byte	.LBE247-.Ltext0
	.4byte	.LBB250-.Ltext0
	.4byte	.LBE250-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB256-.Ltext0
	.4byte	.LBE256-.Ltext0
	.4byte	.LBB259-.Ltext0
	.4byte	.LBE259-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB262-.Ltext0
	.4byte	.LBE262-.Ltext0
	.4byte	.LBB265-.Ltext0
	.4byte	.LBE265-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB276-.Ltext0
	.4byte	.LBE276-.Ltext0
	.4byte	.LBB279-.Ltext0
	.4byte	.LBE279-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB294-.Ltext0
	.4byte	.LBE294-.Ltext0
	.4byte	.LBB295-.Ltext0
	.4byte	.LBE295-.Ltext0
	.4byte	.LBB296-.Ltext0
	.4byte	.LBE296-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB315-.Ltext0
	.4byte	.LBE315-.Ltext0
	.4byte	.LBB320-.Ltext0
	.4byte	.LBE320-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB321-.Ltext0
	.4byte	.LBE321-.Ltext0
	.4byte	.LBB326-.Ltext0
	.4byte	.LBE326-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB337-.Ltext0
	.4byte	.LBE337-.Ltext0
	.4byte	.LBB344-.Ltext0
	.4byte	.LBE344-.Ltext0
	.4byte	.LBB345-.Ltext0
	.4byte	.LBE345-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB361-.Ltext0
	.4byte	.LBE361-.Ltext0
	.4byte	.LBB366-.Ltext0
	.4byte	.LBE366-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB367-.Ltext0
	.4byte	.LBE367-.Ltext0
	.4byte	.LBB368-.Ltext0
	.4byte	.LBE368-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB375-.Ltext0
	.4byte	.LBE375-.Ltext0
	.4byte	.LBB377-.Ltext0
	.4byte	.LBE377-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB419-.Ltext0
	.4byte	.LBE419-.Ltext0
	.4byte	.LBB426-.Ltext0
	.4byte	.LBE426-.Ltext0
	.4byte	.LBB427-.Ltext0
	.4byte	.LBE427-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB428-.Ltext0
	.4byte	.LBE428-.Ltext0
	.4byte	.LBB431-.Ltext0
	.4byte	.LBE431-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB434-.Ltext0
	.4byte	.LBE434-.Ltext0
	.4byte	.LBB437-.Ltext0
	.4byte	.LBE437-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB446-.Ltext0
	.4byte	.LBE446-.Ltext0
	.4byte	.LBB447-.Ltext0
	.4byte	.LBE447-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB450-.Ltext0
	.4byte	.LBE450-.Ltext0
	.4byte	.LBB451-.Ltext0
	.4byte	.LBE451-.Ltext0
	.4byte	.LBB452-.Ltext0
	.4byte	.LBE452-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB464-.Ltext0
	.4byte	.LBE464-.Ltext0
	.4byte	.LBB469-.Ltext0
	.4byte	.LBE469-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB496-.Ltext0
	.4byte	.LBE496-.Ltext0
	.4byte	.LBB504-.Ltext0
	.4byte	.LBE504-.Ltext0
	.4byte	.LBB505-.Ltext0
	.4byte	.LBE505-.Ltext0
	.4byte	.LBB506-.Ltext0
	.4byte	.LBE506-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB498-.Ltext0
	.4byte	.LBE498-.Ltext0
	.4byte	.LBB499-.Ltext0
	.4byte	.LBE499-.Ltext0
	.4byte	.LBB500-.Ltext0
	.4byte	.LBE500-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB517-.Ltext0
	.4byte	.LBE517-.Ltext0
	.4byte	.LBB518-.Ltext0
	.4byte	.LBE518-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB524-.Ltext0
	.4byte	.LBE524-.Ltext0
	.4byte	.LBB525-.Ltext0
	.4byte	.LBE525-.Ltext0
	.4byte	0
	.4byte	0
	.section	.debug_line,"",%progbits
.Ldebug_line0:
	.section	.debug_str,"MS",%progbits,1
.LASF470:
	.ascii	"recovery_flash_mode\000"
.LASF46:
	.ascii	"root_mountflags\000"
.LASF400:
	.ascii	"error_phy_page\000"
.LASF338:
	.ascii	"gc_ink_free_return_value\000"
.LASF171:
	.ascii	"available_pages\000"
.LASF466:
	.ascii	"mlc_detected_active_page\000"
.LASF388:
	.ascii	"spperBlk\000"
.LASF14:
	.ascii	"size_t\000"
.LASF189:
	.ascii	"c_ftl_nand_planes_num\000"
.LASF1:
	.ascii	"sizetype\000"
.LASF593:
	.ascii	"re_try_next_blk\000"
.LASF23:
	.ascii	"saved_command_line\000"
.LASF588:
	.ascii	"P2V_block_in_plane\000"
.LASF343:
	.ascii	"ftl_memcmp\000"
.LASF216:
	.ascii	"g_GlobalSysVersion\000"
.LASF264:
	.ascii	"g_totle_vendor_block\000"
.LASF96:
	.ascii	"BlkNum\000"
.LASF598:
	.ascii	"totle_count\000"
.LASF488:
	.ascii	"lastWritePage\000"
.LASF445:
	.ascii	"ftl_check_vpc\000"
.LASF355:
	.ascii	"request\000"
.LASF326:
	.ascii	"DeviceCapacity\000"
.LASF558:
	.ascii	"List_remove_node\000"
.LASF135:
	.ascii	"PowerOnTimes\000"
.LASF498:
	.ascii	"FtlLoadEctTbl\000"
.LASF385:
	.ascii	"req_read_temp\000"
.LASF69:
	.ascii	"plane_per_die\000"
.LASF58:
	.ascii	"uint32\000"
.LASF449:
	.ascii	"FtlSuperblockPowerLostFix\000"
.LASF225:
	.ascii	"gL2pMapInfo\000"
.LASF501:
	.ascii	"pMapHeader\000"
.LASF362:
	.ascii	"__func__\000"
.LASF477:
	.ascii	"prev_ppa_ver\000"
.LASF86:
	.ascii	"prev_id\000"
.LASF103:
	.ascii	"dump_write_en\000"
.LASF148:
	.ascii	"max_erase_count\000"
.LASF228:
	.ascii	"req_read\000"
.LASF395:
	.ascii	"FtlGcScanTempBlk\000"
.LASF404:
	.ascii	"BOPS_EN\000"
.LASF421:
	.ascii	"get_new_active_ppa\000"
.LASF323:
	.ascii	"g_nand_phy_info\000"
.LASF403:
	.ascii	"FtlGcFreeTempBlock\000"
.LASF251:
	.ascii	"g_totle_swl_count\000"
.LASF563:
	.ascii	"tmp_id\000"
.LASF94:
	.ascii	"sign\000"
.LASF232:
	.ascii	"req_gc_dst\000"
.LASF442:
	.ascii	"FtlSysBlkInit\000"
.LASF467:
	.ascii	"lookup_superblock_id\000"
.LASF141:
	.ascii	"gc_page_count\000"
.LASF566:
	.ascii	"tmp_data_count\000"
.LASF190:
	.ascii	"c_ftl_nand_blk_pre_plane\000"
.LASF75:
	.ascii	"sec_per_blk\000"
.LASF370:
	.ascii	"sftl_gc\000"
.LASF382:
	.ascii	"get_new_gc_superblock\000"
.LASF562:
	.ascii	"tmp_erase_count\000"
.LASF424:
	.ascii	"allocate_new_data_superblock\000"
.LASF664:
	.ascii	"GNU C89 6.3.1 20170404 -mlittle-endian -mabi=aapcs-"
	.ascii	"linux -mno-thumb-interwork -mfpu=vfp -mthumb -march"
	.ascii	"=armv7-a -mfloat-abi=soft -mtune=cortex-a9 -mtls-di"
	.ascii	"alect=gnu -g -Os -std=gnu90 -fno-strict-aliasing -f"
	.ascii	"no-common -fno-PIE -fno-dwarf2-cfi-asm -fno-ipa-sra"
	.ascii	" -funwind-tables -fno-delete-null-pointer-checks -f"
	.ascii	"no-stack-protector -fomit-frame-pointer -fno-var-tr"
	.ascii	"acking-assignments -fno-strict-overflow -fno-merge-"
	.ascii	"all-constants -fmerge-constants -fstack-check=no -f"
	.ascii	"conserve-stack --param allow-store-data-races=0\000"
.LASF381:
	.ascii	"gc_next_superblock\000"
.LASF22:
	.ascii	"boot_command_line\000"
.LASF533:
	.ascii	"update_map_block\000"
.LASF607:
	.ascii	"FtlBbmMapBadBlock\000"
.LASF605:
	.ascii	"FtlBbtInfoPrint\000"
.LASF55:
	.ascii	"hex_asc_upper\000"
.LASF461:
	.ascii	"saved_active_page\000"
.LASF505:
	.ascii	"re_save_vpndata\000"
.LASF535:
	.ascii	"region_num\000"
.LASF602:
	.ascii	"re_save_bbmdata\000"
.LASF622:
	.ascii	"first_lpa_sctidx\000"
.LASF20:
	.ascii	"__security_initcall_start\000"
.LASF307:
	.ascii	"g_totle_slc_erase_count\000"
.LASF596:
	.ascii	"FtlLoadBbt\000"
.LASF175:
	.ascii	"check_en\000"
.LASF221:
	.ascii	"g_MaxLbn\000"
.LASF62:
	.ascii	"page_addr\000"
.LASF397:
	.ascii	"totle_num\000"
.LASF324:
	.ascii	"g_nand_ops\000"
.LASF587:
	.ascii	"ver2\000"
.LASF214:
	.ascii	"c_ftl_nand_max_data_blks\000"
.LASF213:
	.ascii	"c_ftl_nand_data_op_blks_per_plane\000"
.LASF570:
	.ascii	"FtlFreeSysBlkQueueOut\000"
.LASF116:
	.ascii	"ftl_data_blk_header\000"
.LASF230:
	.ascii	"req_erase\000"
.LASF614:
	.ascii	"num_lpa\000"
.LASF199:
	.ascii	"c_ftl_nand_byte_pre_oob\000"
.LASF510:
	.ascii	"FtlEctTblFlush\000"
.LASF120:
	.ascii	"ftl_sys_blk_header\000"
.LASF344:
	.ascii	"ftl_memcpy\000"
.LASF300:
	.ascii	"g_totle_read_page_count\000"
.LASF236:
	.ascii	"p_sys_data_buf_1\000"
.LASF44:
	.ascii	"sysctl_panic_on_stackoverflow\000"
.LASF202:
	.ascii	"c_ftl_nand_map_region_num\000"
.LASF52:
	.ascii	"SYSTEM_RESTART\000"
.LASF125:
	.ascii	"activeFlashMode\000"
.LASF363:
	.ascii	"read_req\000"
.LASF260:
	.ascii	"p_vendor_block_table\000"
.LASF147:
	.ascii	"discard_page_count\000"
.LASF543:
	.ascii	"vpcTbl\000"
.LASF275:
	.ascii	"g_buffer_superblock\000"
.LASF10:
	.ascii	"long int\000"
.LASF635:
	.ascii	"create_first_buffer_superblock\000"
.LASF63:
	.ascii	"p_data\000"
.LASF245:
	.ascii	"c_gc_page_buf_num\000"
.LASF654:
	.ascii	"limit\000"
.LASF514:
	.ascii	"Index\000"
.LASF224:
	.ascii	"gSysFreeQueue\000"
.LASF144:
	.ascii	"l2p_write_count\000"
.LASF621:
	.ascii	"absolute_sector\000"
.LASF556:
	.ascii	"pHead\000"
.LASF66:
	.ascii	"nand_phy_info\000"
.LASF184:
	.ascii	"c_ftl_nand_type\000"
.LASF334:
	.ascii	"FtlUpdateVaildLpnCount\000"
.LASF351:
	.ascii	"FlashTestBlk\000"
.LASF672:
	.ascii	"__builtin_strcpy\000"
.LASF290:
	.ascii	"g_gc_bad_block_gc_index\000"
.LASF269:
	.ascii	"p_free_data_block_list_head\000"
.LASF100:
	.ascii	"pBlkVpcTbl\000"
.LASF582:
	.ascii	"high\000"
.LASF637:
	.ascii	"is_sys_blk\000"
.LASF263:
	.ascii	"p_vendor_region_ppn_table\000"
.LASF545:
	.ascii	"load_l2p_region\000"
.LASF531:
	.ascii	"flush_l2p_region\000"
.LASF37:
	.ascii	"panic_blink\000"
.LASF457:
	.ascii	"num_free_node\000"
.LASF359:
	.ascii	"block_size\000"
.LASF346:
	.ascii	"pvForm\000"
.LASF208:
	.ascii	"c_ftl_vendor_part_size\000"
.LASF559:
	.ascii	"pTmp\000"
.LASF389:
	.ascii	"FtlGcMarkBadPhyBlk\000"
.LASF113:
	.ascii	"ftl_bbt_blk_header\000"
.LASF443:
	.ascii	"ftl_scan_all_data\000"
.LASF353:
	.ascii	"spare\000"
.LASF569:
	.ascii	"INSERT_FREE_LIST\000"
.LASF54:
	.ascii	"hex_asc\000"
.LASF4:
	.ascii	"signed char\000"
.LASF298:
	.ascii	"g_totle_read_sector\000"
.LASF65:
	.ascii	"nand_req\000"
.LASF262:
	.ascii	"p_vendor_block_ver_table\000"
.LASF322:
	.ascii	"gNandPhyInfo\000"
.LASF483:
	.ascii	"FtlL2PDataInit\000"
.LASF522:
	.ascii	"action\000"
.LASF405:
	.ascii	"superblock_id\000"
.LASF313:
	.ascii	"g_gc_head_data_block\000"
.LASF53:
	.ascii	"system_state\000"
.LASF620:
	.ascii	"FtlRead\000"
.LASF272:
	.ascii	"g_num_free_superblocks\000"
.LASF592:
	.ascii	"pBbtHeader\000"
.LASF5:
	.ascii	"unsigned char\000"
.LASF345:
	.ascii	"pvTo\000"
.LASF47:
	.ascii	"early_boot_irqs_disabled\000"
.LASF204:
	.ascii	"c_ftl_nand_vendor_region_num\000"
.LASF463:
	.ascii	"next_free_active_plane\000"
.LASF520:
	.ascii	"FtlVendorPartWrite\000"
.LASF48:
	.ascii	"SYSTEM_BOOTING\000"
.LASF119:
	.ascii	"regionId\000"
.LASF612:
	.ascii	"start_lpa\000"
.LASF310:
	.ascii	"g_in_gc_progress\000"
.LASF18:
	.ascii	"__con_initcall_start\000"
.LASF83:
	.ascii	"read_page\000"
.LASF283:
	.ascii	"g_gc_blk_num\000"
.LASF431:
	.ascii	"GetSwlReplaceBlock\000"
.LASF90:
	.ascii	"ftl_sys_info\000"
.LASF158:
	.ascii	"refresh_start_lpa\000"
.LASF459:
	.ascii	"FtlPowerLostRecovery\000"
.LASF560:
	.ascii	"insert_free_list\000"
.LASF13:
	.ascii	"_Bool\000"
.LASF460:
	.ascii	"FtlRecoverySuperblock\000"
.LASF28:
	.ascii	"linux_proc_banner\000"
.LASF429:
	.ascii	"get_new_id\000"
.LASF316:
	.ascii	"g_cur_erase_blk\000"
.LASF349:
	.ascii	"table\000"
.LASF165:
	.ascii	"value\000"
.LASF223:
	.ascii	"gSysInfo\000"
.LASF374:
	.ascii	"gc_page_num\000"
.LASF348:
	.ascii	"FlashGetBadBlockList\000"
.LASF89:
	.ascii	"BbtMap\000"
.LASF2:
	.ascii	"char\000"
.LASF649:
	.ascii	"FtlSysBlkNumInit\000"
.LASF658:
	.ascii	"memset\000"
.LASF328:
	.ascii	"TotleReadBufferCount1\000"
.LASF160:
	.ascii	"totle_read_sector\000"
.LASF330:
	.ascii	"TotleReadBufferCount3\000"
.LASF627:
	.ascii	"FtlSysFlush\000"
.LASF257:
	.ascii	"p_map_block_ver_table\000"
.LASF131:
	.ascii	"bufferPageOffset\000"
.LASF524:
	.ascii	"l2p_region_id\000"
.LASF410:
	.ascii	"FtlGcUpdatePage\000"
.LASF436:
	.ascii	"maxFreeBlockEraseCount\000"
.LASF642:
	.ascii	"re_test_next_page\000"
.LASF42:
	.ascii	"panic_on_io_nmi\000"
.LASF425:
	.ascii	"allocate_data_superblock\000"
.LASF575:
	.ascii	"FtlFreeSysBlkQueueIn\000"
.LASF491:
	.ascii	"pMapBlkHeader\000"
.LASF71:
	.ascii	"blk_per_die\000"
.LASF329:
	.ascii	"TotleReadBufferCount2\000"
.LASF377:
	.ascii	"num_io\000"
.LASF584:
	.ascii	"pagePreBlk\000"
.LASF129:
	.ascii	"activePageOffset\000"
.LASF77:
	.ascii	"reserved_blk\000"
.LASF482:
	.ascii	"FtlLoadMapInfo\000"
.LASF366:
	.ascii	"block_index\000"
.LASF651:
	.ascii	"FtlPrintInfo\000"
.LASF480:
	.ascii	"FtlReUsePrevPpa\000"
.LASF250:
	.ascii	"p_swl_mul_table\000"
.LASF564:
	.ascii	"insert_data_list\000"
.LASF645:
	.ascii	"FtlVariablesInit\000"
.LASF268:
	.ascii	"p_data_block_list_table\000"
.LASF512:
	.ascii	"ec_mod_count\000"
.LASF435:
	.ascii	"pNode\000"
.LASF304:
	.ascii	"g_totle_mlc_erase_count\000"
.LASF585:
	.ascii	"ftl_cmp_data_ver\000"
.LASF276:
	.ascii	"g_gc_temp_superblock\000"
.LASF261:
	.ascii	"p_vendor_block_valid_page_count\000"
.LASF499:
	.ascii	"FtlScanSysBlk\000"
.LASF668:
	.ascii	"l2p_addr_tran\000"
.LASF309:
	.ascii	"g_max_erase_count\000"
.LASF210:
	.ascii	"c_ftl_nand_init_sys_blks_per_plane\000"
.LASF182:
	.ascii	"ftl_gc_page_buffer\000"
.LASF233:
	.ascii	"g_req_cache\000"
.LASF648:
	.ascii	"FtlConstantsInit\000"
.LASF451:
	.ascii	"pages\000"
.LASF420:
	.ascii	"update_vpc_list\000"
.LASF661:
	.ascii	"rknand_print_hex\000"
.LASF408:
	.ascii	"p_gc_page\000"
.LASF15:
	.ascii	"elf_hwcap\000"
.LASF394:
	.ascii	"Ftl_get_new_temp_ppa\000"
.LASF136:
	.ascii	"PowerLostTimes\000"
.LASF577:
	.ascii	"blk_Id\000"
.LASF293:
	.ascii	"g_gc_merge_free_blk_threshold\000"
.LASF38:
	.ascii	"oops_in_progress\000"
.LASF432:
	.ascii	"node_id\000"
.LASF134:
	.ascii	"cache_write_count\000"
.LASF259:
	.ascii	"g_totle_map_block\000"
.LASF659:
	.ascii	"printk\000"
.LASF546:
	.ascii	"phyAddr\000"
.LASF186:
	.ascii	"c_ftl_nand_planes_per_die\000"
.LASF489:
	.ascii	"ppnTbl\000"
.LASF364:
	.ascii	"FlashReadPages\000"
.LASF375:
	.ascii	"gc_blk\000"
.LASF484:
	.ascii	"FtlLoadVonderInfo\000"
.LASF554:
	.ascii	"List_get_gc_head_node\000"
.LASF503:
	.ascii	"pSysHeader\000"
.LASF180:
	.ascii	"scr_ppa\000"
.LASF209:
	.ascii	"c_ftl_nand_sys_blks_per_plane\000"
.LASF572:
	.ascii	"FtlFreeSysBLkSort\000"
.LASF266:
	.ascii	"p_l2p_map_buf\000"
.LASF643:
	.ascii	"IsBlkInVendorPart\000"
.LASF599:
	.ascii	"FtlBbtMemInit\000"
.LASF244:
	.ascii	"gp_gc_page_buf_info\000"
.LASF573:
	.ascii	"tmp_blk_id\000"
.LASF167:
	.ascii	"front\000"
.LASF235:
	.ascii	"p_sys_data_buf\000"
.LASF93:
	.ascii	"index\000"
.LASF538:
	.ascii	"lastMapBlk\000"
.LASF624:
	.ascii	"last_lpa_nscts\000"
.LASF301:
	.ascii	"g_tmp_data_superblock_id\000"
.LASF247:
	.ascii	"gp_ect_tbl_info\000"
.LASF655:
	.ascii	"rknand_proc_ftlread\000"
.LASF542:
	.ascii	"ftl_free_no_use_map_blk\000"
.LASF666:
	.ascii	"/home/ldq/rk-linux/kernel-debug\000"
.LASF35:
	.ascii	"atomic_notifier_head\000"
.LASF384:
	.ascii	"FtlReadRefresh\000"
.LASF187:
	.ascii	"c_ftl_nand_blks_per_die\000"
.LASF565:
	.ascii	"node_data_count\000"
.LASF601:
	.ascii	"write_count\000"
.LASF241:
	.ascii	"p_io_spare_buf\000"
.LASF479:
	.ascii	"current_ppa_ver\000"
.LASF0:
	.ascii	"long unsigned int\000"
.LASF173:
	.ascii	"num_planes\000"
.LASF121:
	.ascii	"res32_0\000"
.LASF122:
	.ascii	"res32_1\000"
.LASF433:
	.ascii	"min_ec_id\000"
.LASF361:
	.ascii	"check\000"
.LASF29:
	.ascii	"console_printk\000"
.LASF671:
	.ascii	"strcpy\000"
.LASF61:
	.ascii	"status\000"
.LASF68:
	.ascii	"die_num\000"
.LASF628:
	.ascii	"sftl_init\000"
.LASF292:
	.ascii	"g_gc_free_blk_threshold\000"
.LASF32:
	.ascii	"kptr_restrict\000"
.LASF594:
	.ascii	"FtlLoadFactoryBbt\000"
.LASF407:
	.ascii	"scan_page_num\000"
.LASF149:
	.ascii	"min_erase_count\000"
.LASF185:
	.ascii	"c_ftl_nand_die_num\000"
.LASF306:
	.ascii	"g_min_erase_count\000"
.LASF481:
	.ascii	"ftl_sb_update_avl_pages\000"
.LASF105:
	.ascii	"region_id\000"
.LASF631:
	.ascii	"ftl_low_format\000"
.LASF342:
	.ascii	"str2\000"
.LASF95:
	.ascii	"maxRegion\000"
.LASF325:
	.ascii	"ftl_temp_buf\000"
.LASF161:
	.ascii	"progErrorCount\000"
.LASF523:
	.ascii	"ram_region_id\000"
.LASF335:
	.ascii	"check_vpc_table\000"
.LASF453:
	.ascii	"FtlGcPageRecovery\000"
.LASF30:
	.ascii	"printk_delay_msec\000"
.LASF493:
	.ascii	"ftl_get_blk_mode\000"
.LASF27:
	.ascii	"linux_banner\000"
.LASF663:
	.ascii	"sprintf\000"
.LASF108:
	.ascii	"ftl_ect_tbl_info\000"
.LASF540:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
.LASF513:
	.ascii	"FtlVendorPartRead\000"
.LASF611:
	.ascii	"FtlWrite\000"
.LASF74:
	.ascii	"sec_per_page\000"
.LASF70:
	.ascii	"blk_per_plane\000"
.LASF401:
	.ascii	"ReInit\000"
.LASF518:
	.ascii	"result\000"
.LASF99:
	.ascii	"pBlkVerTbl\000"
.LASF450:
	.ascii	"req_temp\000"
.LASF117:
	.ascii	"lastPpa\000"
.LASF321:
	.ascii	"g_power_lost_recovery_flag\000"
.LASF603:
	.ascii	"FtlBbt2Bitmap\000"
.LASF72:
	.ascii	"page_per_blk\000"
.LASF472:
	.ascii	"recovery_super_page\000"
.LASF312:
	.ascii	"g_ftl_nand_free_count\000"
.LASF368:
	.ascii	"die_index\000"
.LASF415:
	.ascii	"numREQ\000"
.LASF78:
	.ascii	"ecc_bits\000"
.LASF633:
	.ascii	"sys_bad_block_cnt\000"
.LASF195:
	.ascii	"c_ftl_nand_page_pre_super_blk\000"
.LASF177:
	.ascii	"reversed\000"
.LASF402:
	.ascii	"read_super_page\000"
.LASF39:
	.ascii	"panic_timeout\000"
.LASF127:
	.ascii	"gcTempFlashMode\000"
.LASF295:
	.ascii	"g_totle_gc_page_count\000"
.LASF237:
	.ascii	"p_vendor_data_buf\000"
.LASF600:
	.ascii	"FtlBbmTblFlush\000"
.LASF567:
	.ascii	"tmp_multiplier_value\000"
.LASF660:
	.ascii	"__memzero\000"
.LASF406:
	.ascii	"lookup_ppa\000"
.LASF8:
	.ascii	"long long int\000"
.LASF305:
	.ascii	"g_totle_avg_erase_count\000"
.LASF215:
	.ascii	"ftl_gc_temp_power_lost_recovery_flag\000"
.LASF551:
	.ascii	"prev_valid_page_count\000"
.LASF387:
	.ascii	"FtlGcFreeBadSuperBlk\000"
.LASF11:
	.ascii	"__kernel_size_t\000"
.LASF222:
	.ascii	"gBbtInfo\000"
.LASF226:
	.ascii	"gVendorBlkInfo\000"
.LASF422:
	.ascii	"p_superblock\000"
.LASF270:
	.ascii	"p_data_block_list_head\000"
.LASF475:
	.ascii	"last_page_version\000"
.LASF231:
	.ascii	"req_gc\000"
.LASF610:
	.ascii	"FtlDiscard\000"
.LASF36:
	.ascii	"panic_notifier_list\000"
.LASF287:
	.ascii	"g_gc_next_blk\000"
.LASF386:
	.ascii	"add_count\000"
.LASF154:
	.ascii	"inkDie_write_and_check_en\000"
.LASF581:
	.ascii	"FtlGetLastWrittenPage\000"
.LASF465:
	.ascii	"detected_active_page\000"
.LASF509:
	.ascii	"FtlUpdateVaildLpn\000"
.LASF24:
	.ascii	"reset_devices\000"
.LASF157:
	.ascii	"refresh_enable_mode\000"
.LASF606:
	.ascii	"FtlBbmIsBadBlock\000"
.LASF176:
	.ascii	"dump_writed\000"
.LASF391:
	.ascii	"FtlGcRefreshBlock\000"
.LASF88:
	.ascii	"fBbtBlk\000"
.LASF536:
	.ascii	"blkTbl\000"
.LASF380:
	.ascii	"p_dataHeader\000"
.LASF462:
	.ascii	"saved_active_plane\000"
.LASF126:
	.ascii	"bufferFlashMode\000"
.LASF114:
	.ascii	"count\000"
.LASF133:
	.ascii	"gcTempPageOffset\000"
.LASF358:
	.ascii	"addr\000"
.LASF139:
	.ascii	"GlobalSysVersion\000"
.LASF411:
	.ascii	"src_ppa\000"
.LASF557:
	.ascii	"List_pop_index_node\000"
.LASF273:
	.ascii	"g_num_data_superblocks\000"
.LASF447:
	.ascii	"error_flag\000"
.LASF665:
	.ascii	"drivers/rkflash/rksftl/rk_sftl.c\000"
.LASF311:
	.ascii	"g_in_swl_replace\000"
.LASF169:
	.ascii	"ftl_superblock_info\000"
.LASF3:
	.ascii	"unsigned int\000"
.LASF98:
	.ascii	"pBlkTbl\000"
.LASF168:
	.ascii	"rear\000"
.LASF192:
	.ascii	"c_ftl_nand_bbm_buf_size\000"
.LASF219:
	.ascii	"g_MaxLpn\000"
.LASF541:
	.ascii	"blk_index\000"
.LASF456:
	.ascii	"num_data_node\000"
.LASF50:
	.ascii	"SYSTEM_HALT\000"
.LASF291:
	.ascii	"g_gc_bad_block_temp_tbl\000"
.LASF33:
	.ascii	"kmsg_fops\000"
.LASF568:
	.ascii	"INSERT_DATA_LIST\000"
.LASF615:
	.ascii	"sctidx\000"
.LASF390:
	.ascii	"superBlk\000"
.LASF183:
	.ascii	"flag\000"
.LASF284:
	.ascii	"g_gc_page_offset\000"
.LASF641:
	.ascii	"test_mode\000"
.LASF299:
	.ascii	"g_totle_discard_page_count\000"
.LASF217:
	.ascii	"g_GlobalDataVersion\000"
.LASF652:
	.ascii	"debug_flag\000"
.LASF327:
	.ascii	"gFtlInitStatus\000"
.LASF530:
	.ascii	"hit_count\000"
.LASF82:
	.ascii	"prog_page\000"
.LASF130:
	.ascii	"bufferSuperblockId\000"
.LASF492:
	.ascii	"FtlLoadSysInfo\000"
.LASF155:
	.ascii	"readErrorCount\000"
.LASF172:
	.ascii	"current_plane\000"
.LASF399:
	.ascii	"page_count\000"
.LASF277:
	.ascii	"g_gc_superblock\000"
.LASF274:
	.ascii	"g_active_superblock\000"
.LASF517:
	.ascii	"offset\000"
.LASF252:
	.ascii	"p_valid_page_count_table\000"
.LASF197:
	.ascii	"c_ftl_nand_sec_pre_page_shift\000"
.LASF26:
	.ascii	"initcall_debug\000"
.LASF392:
	.ascii	"block\000"
.LASF354:
	.ascii	"FlashEraseBlocks\000"
.LASF85:
	.ascii	"page\000"
.LASF413:
	.ascii	"IsBlkInGcList\000"
.LASF341:
	.ascii	"str1\000"
.LASF67:
	.ascii	"nand_type\000"
.LASF616:
	.ascii	"nscts\000"
.LASF104:
	.ascii	"ftl_l2p_ram_map_info\000"
.LASF297:
	.ascii	"g_totle_write_sector\000"
.LASF446:
	.ascii	"prev_ppa\000"
.LASF80:
	.ascii	"get_bad_blk_list\000"
.LASF296:
	.ascii	"g_totle_write_page_count\000"
.LASF646:
	.ascii	"FtlMemInit\000"
.LASF303:
	.ascii	"g_totle_l2p_write_count\000"
.LASF589:
	.ascii	"P2V_plane\000"
.LASF12:
	.ascii	"bool\000"
.LASF256:
	.ascii	"p_map_block_valid_page_count\000"
.LASF339:
	.ascii	"check_buf\000"
.LASF561:
	.ascii	"node_erase_count\000"
.LASF555:
	.ascii	"List_pop_head_node\000"
.LASF145:
	.ascii	"slc_erase_count\000"
.LASF544:
	.ascii	"minValidPageCount\000"
.LASF644:
	.ascii	"ven_blk\000"
.LASF193:
	.ascii	"c_ftl_nand_page_pre_blk\000"
.LASF107:
	.ascii	"L2PMap\000"
.LASF347:
	.ascii	"size\000"
.LASF547:
	.ascii	"List_update_data_list\000"
.LASF308:
	.ascii	"g_totle_sys_slc_erase_count\000"
.LASF124:
	.ascii	"maxDieNum\000"
.LASF246:
	.ascii	"g_gc_num_req\000"
.LASF91:
	.ascii	"maxLogicBlk\000"
.LASF150:
	.ascii	"totle_power_on_run_times\000"
.LASF521:
	.ascii	"log2phys\000"
.LASF469:
	.ascii	"detected_error_page\000"
.LASF583:
	.ascii	"spareBuf\000"
.LASF188:
	.ascii	"c_ftl_nand_blks_per_die_shift\000"
.LASF140:
	.ascii	"ftl_sys_ext_info\000"
.LASF265:
	.ascii	"p_l2p_ram_map\000"
.LASF97:
	.ascii	"maxBlkNum\000"
.LASF662:
	.ascii	"ftl_malloc\000"
.LASF444:
	.ascii	"pDataHeader\000"
.LASF170:
	.ascii	"current_page\000"
.LASF81:
	.ascii	"erase_blk\000"
.LASF455:
	.ascii	"SupperBlkListInit\000"
.LASF634:
	.ascii	"create_first_active_superblock\000"
.LASF166:
	.ascii	"sys_blk_queue\000"
.LASF34:
	.ascii	"file_operations\000"
.LASF360:
	.ascii	"FlashProgPages\000"
.LASF519:
	.ascii	"ppn_index\000"
.LASF9:
	.ascii	"long long unsigned int\000"
.LASF220:
	.ascii	"g_VaildLpn\000"
.LASF249:
	.ascii	"p_erase_count_table\000"
.LASF528:
	.ascii	"select_l2p_ram_region\000"
.LASF438:
	.ascii	"GetFreeBlockMaxEraseCount\000"
.LASF552:
	.ascii	"prev_multiplier_value\000"
.LASF21:
	.ascii	"__security_initcall_end\000"
.LASF40:
	.ascii	"panic_on_oops\000"
.LASF267:
	.ascii	"g_l2p_last_update_region_id\000"
.LASF201:
	.ascii	"c_ftl_nand_totle_phy_blks\000"
.LASF636:
	.ascii	"FtlLowFormatEraseBlock\000"
.LASF76:
	.ascii	"byte_per_sec\000"
.LASF181:
	.ascii	"des_ppa\000"
.LASF255:
	.ascii	"p_map_block_table\000"
.LASF548:
	.ascii	"prev_node_id\000"
.LASF243:
	.ascii	"p_gc_data_buf\000"
.LASF532:
	.ascii	"FtlMapWritePage\000"
.LASF16:
	.ascii	"elf_hwcap2\000"
.LASF254:
	.ascii	"p_blk_mode_table\000"
.LASF285:
	.ascii	"g_gc_cur_blk_valid_pages\000"
.LASF579:
	.ascii	"FtlFreeSysBlkQueueEmpty\000"
.LASF194:
	.ascii	"c_ftl_nand_page_pre_slc_blk\000"
.LASF515:
	.ascii	"nSec\000"
.LASF597:
	.ascii	"FtlBbtCalcTotleCnt\000"
.LASF196:
	.ascii	"c_ftl_nand_sec_pre_page\000"
.LASF495:
	.ascii	"FtlSlcSuperblockCheck\000"
.LASF586:
	.ascii	"ver1\000"
.LASF153:
	.ascii	"SlcPartLbaEndSector\000"
.LASF206:
	.ascii	"c_ftl_nand_max_map_blks\000"
.LASF497:
	.ascii	"FtlGcReFreshBadBlk\000"
.LASF476:
	.ascii	"last_mlc_page_version\000"
.LASF508:
	.ascii	"FtlMapBlkWriteDump_data\000"
.LASF248:
	.ascii	"g_ect_tbl_info_size\000"
.LASF110:
	.ascii	"totleEc\000"
.LASF440:
	.ascii	"GetFreeBlockMinEraseCount\000"
.LASF437:
	.ascii	"minDataBlockEraseCount\000"
.LASF51:
	.ascii	"SYSTEM_POWER_OFF\000"
.LASF218:
	.ascii	"g_MaxLbaSector\000"
.LASF87:
	.ascii	"version\000"
.LASF31:
	.ascii	"dmesg_restrict\000"
.LASF464:
	.ascii	"next_free_active_page\000"
.LASF132:
	.ascii	"gcTempSuperblockId\000"
.LASF623:
	.ascii	"first_lpa_nscts\000"
.LASF302:
	.ascii	"g_totle_cache_write_count\000"
.LASF496:
	.ascii	"ftl_set_blk_mode\000"
.LASF452:
	.ascii	"Ftl_load_ext_data\000"
.LASF506:
	.ascii	"FtlWriteDump_data\000"
.LASF638:
	.ascii	"max_test_page_num\000"
.LASF393:
	.ascii	"Ftl_gc_temp_data_write_back\000"
.LASF369:
	.ascii	"block_in_die\000"
.LASF418:
	.ascii	"FtlGcBufInit\000"
.LASF234:
	.ascii	"p_plane_order_table\000"
.LASF101:
	.ascii	"pMapPpnTbl\000"
.LASF430:
	.ascii	"free_data_superblock\000"
.LASF146:
	.ascii	"sys_slc_erase_count\000"
.LASF207:
	.ascii	"c_ftl_nand_max_vendor_blks\000"
.LASF378:
	.ascii	"physical_block\000"
.LASF448:
	.ascii	"FtlVpcCheckAndModify\000"
.LASF279:
	.ascii	"g_sys_save_data\000"
.LASF84:
	.ascii	"ftl_bbt_info\000"
.LASF428:
	.ascii	"free_blk_index\000"
.LASF253:
	.ascii	"p_valid_page_count_check_table\000"
.LASF212:
	.ascii	"c_ftl_nand_data_blks_per_plane\000"
.LASF371:
	.ascii	"rk_ftl_garbage_collect\000"
.LASF657:
	.ascii	"memcmp\000"
.LASF373:
	.ascii	"numPages\000"
.LASF314:
	.ascii	"g_gc_head_data_block_count\000"
.LASF258:
	.ascii	"p_map_region_ppn_table\000"
.LASF527:
	.ascii	"l2p_flush\000"
.LASF458:
	.ascii	"make_superblock\000"
.LASF19:
	.ascii	"__con_initcall_end\000"
.LASF632:
	.ascii	"bad_block_cnt\000"
.LASF550:
	.ascii	"node_multiplier_value\000"
.LASF333:
	.ascii	"g_ect_tbl_power_up_flush\000"
.LASF319:
	.ascii	"g_recovery_ppa_tbl\000"
.LASF526:
	.ascii	"found_lpa\000"
.LASF280:
	.ascii	"g_sys_ext_data\000"
.LASF356:
	.ascii	"flash_type\000"
.LASF416:
	.ascii	"req_num\000"
.LASF511:
	.ascii	"forceFlush\000"
.LASF56:
	.ascii	"uint8\000"
.LASF332:
	.ascii	"power_up_flag\000"
.LASF282:
	.ascii	"p_gc_blk_tbl\000"
.LASF414:
	.ascii	"FtlGcBufAlloc\000"
.LASF191:
	.ascii	"c_ftl_nand_ext_blk_pre_plane\000"
.LASF379:
	.ascii	"max_gc_page_num\000"
.LASF198:
	.ascii	"c_ftl_nand_byte_pre_page\000"
.LASF441:
	.ascii	"update_multiplier_value\000"
.LASF619:
	.ascii	"flashType\000"
.LASF6:
	.ascii	"short int\000"
.LASF242:
	.ascii	"p_gc_spare_buf\000"
.LASF494:
	.ascii	"sblk\000"
.LASF372:
	.ascii	"mode\000"
.LASF426:
	.ascii	"new_id\000"
.LASF629:
	.ascii	"pNand\000"
.LASF49:
	.ascii	"SYSTEM_RUNNING\000"
.LASF516:
	.ascii	"pBuf\000"
.LASF142:
	.ascii	"write_page_count\000"
.LASF604:
	.ascii	"bitmap\000"
.LASF200:
	.ascii	"c_ftl_nand_reserved_blks\000"
.LASF669:
	.ascii	"sftl_get_density\000"
.LASF640:
	.ascii	"test_page_step\000"
.LASF595:
	.ascii	"blkAddr\000"
.LASF286:
	.ascii	"g_gc_cur_blk_max_valid_pages\000"
.LASF288:
	.ascii	"g_gc_next_blk_1\000"
.LASF529:
	.ascii	"target_region\000"
.LASF613:
	.ascii	"end_lpa\000"
.LASF473:
	.ascii	"scan_completed\000"
.LASF656:
	.ascii	"FtlPrintInfo2buf\000"
.LASF365:
	.ascii	"p_die\000"
.LASF239:
	.ascii	"p_io_data_buf_0\000"
.LASF240:
	.ascii	"p_io_data_buf_1\000"
.LASF137:
	.ascii	"mlc_erase_count\000"
.LASF178:
	.ascii	"phyBlk\000"
.LASF626:
	.ascii	"sftl_deinit\000"
.LASF486:
	.ascii	"pMapBlockInfo\000"
.LASF151:
	.ascii	"last_refresh_data_times\000"
.LASF667:
	.ascii	"system_states\000"
.LASF159:
	.ascii	"totle_write_sector\000"
.LASF229:
	.ascii	"req_prgm\000"
.LASF591:
	.ascii	"FtlMakeBbt\000"
.LASF102:
	.ascii	"in_gc_mode\000"
.LASF434:
	.ascii	"min_ec\000"
.LASF60:
	.ascii	"int16\000"
.LASF281:
	.ascii	"p_gc_page_info\000"
.LASF211:
	.ascii	"c_ftl_nand_max_sys_blks\000"
.LASF439:
	.ascii	"max_ec\000"
.LASF238:
	.ascii	"p_sys_spare_buf\000"
.LASF670:
	.ascii	"memcpy\000"
.LASF504:
	.ascii	"prog_error_count\000"
.LASF336:
	.ascii	"gc_discard_updated\000"
.LASF412:
	.ascii	"prev_superblock_id\000"
.LASF639:
	.ascii	"test_page_num\000"
.LASF156:
	.ascii	"last_refresh_read_count\000"
.LASF478:
	.ascii	"lookup_ppa_ver\000"
.LASF490:
	.ascii	"totleBlkNum\000"
.LASF609:
	.ascii	"sftl_read\000"
.LASF578:
	.ascii	"FtlFreeSysBlkQueueFull\000"
.LASF409:
	.ascii	"FtlGcPageVarInit\000"
.LASF112:
	.ascii	"padding\000"
.LASF174:
	.ascii	"flash_mode\000"
.LASF468:
	.ascii	"recovery_cur_page_ver\000"
.LASF537:
	.ascii	"ftl_map_blk_gc\000"
.LASF608:
	.ascii	"sftl_write\000"
.LASF118:
	.ascii	"ftl_map_blk_header\000"
.LASF580:
	.ascii	"FtlFreeSysBlkQueueInit\000"
.LASF25:
	.ascii	"late_time_init\000"
.LASF647:
	.ascii	"mem_size\000"
.LASF485:
	.ascii	"FtlMapTblRecovery\000"
.LASF423:
	.ascii	"new_ppa\000"
.LASF427:
	.ascii	"num_bad_block\000"
.LASF525:
	.ascii	"lpn_index\000"
.LASF203:
	.ascii	"c_ftl_nand_l2pmap_ram_region_num\000"
.LASF625:
	.ascii	"sector\000"
.LASF507:
	.ascii	"num_page\000"
.LASF618:
	.ascii	"FtlProgPages\000"
.LASF417:
	.ascii	"FtlGcBufFree\000"
.LASF271:
	.ascii	"p_data_block_list_tail\000"
.LASF398:
	.ascii	"current_ppa\000"
.LASF64:
	.ascii	"p_spare\000"
.LASF383:
	.ascii	"tmp_blk\000"
.LASF294:
	.ascii	"g_gc_blk_index\000"
.LASF500:
	.ascii	"block_in_plane\000"
.LASF111:
	.ascii	"lastEc\000"
.LASF352:
	.ascii	"PhyBlk\000"
.LASF106:
	.ascii	"reserved\000"
.LASF317:
	.ascii	"g_recovery_page_num\000"
.LASF549:
	.ascii	"node_valid_page_count\000"
.LASF179:
	.ascii	"ftl_gc_page_item\000"
.LASF143:
	.ascii	"read_page_count\000"
.LASF419:
	.ascii	"decrement_vpc_count\000"
.LASF7:
	.ascii	"short unsigned int\000"
.LASF539:
	.ascii	"minValidPageIndex\000"
.LASF331:
	.ascii	"TotleReadBufferMatchCount\000"
.LASF340:
	.ascii	"check_spare_buf\000"
.LASF574:
	.ascii	"tmp_ec\000"
.LASF315:
	.ascii	"g_gc_skip_write_count\000"
.LASF59:
	.ascii	"int32\000"
.LASF73:
	.ascii	"page_per_slc_blk\000"
.LASF318:
	.ascii	"g_recovery_page_min_ver\000"
.LASF227:
	.ascii	"req_sys\000"
.LASF162:
	.ascii	"List_Node\000"
.LASF653:
	.ascii	"Ftl_log2\000"
.LASF152:
	.ascii	"all_blk_used_slc_mode\000"
.LASF396:
	.ascii	"pSuperblock\000"
.LASF474:
	.ascii	"function_exit\000"
.LASF205:
	.ascii	"c_ftl_nand_map_blks_per_plane\000"
.LASF17:
	.ascii	"initcall_t\000"
.LASF590:
	.ascii	"V2P_block\000"
.LASF115:
	.ascii	"sysBlksPerPlane\000"
.LASF454:
	.ascii	"Ftl_save_ext_data\000"
.LASF617:
	.ascii	"FtlCacheWriteBack\000"
.LASF79:
	.ascii	"nand_ops\000"
.LASF630:
	.ascii	"load_bbt\000"
.LASF650:
	.ascii	"blkNum\000"
.LASF367:
	.ascii	"page_index\000"
.LASF502:
	.ascii	"FtlVpcTblFlush\000"
.LASF109:
	.ascii	"hash\000"
.LASF571:
	.ascii	"IsInFreeQueue\000"
.LASF471:
	.ascii	"read_super_page_end\000"
.LASF357:
	.ascii	"num_req\000"
.LASF320:
	.ascii	"c_mlc_erase_count_value\000"
.LASF553:
	.ascii	"pPrev\000"
.LASF138:
	.ascii	"GlobalDataVersion\000"
.LASF41:
	.ascii	"panic_on_unrecovered_nmi\000"
.LASF163:
	.ascii	"next\000"
.LASF128:
	.ascii	"activeSuperblockId\000"
.LASF376:
	.ascii	"plane\000"
.LASF43:
	.ascii	"panic_on_warn\000"
.LASF92:
	.ascii	"ftl_l2p_map_info\000"
.LASF164:
	.ascii	"prev\000"
.LASF57:
	.ascii	"uint16\000"
.LASF350:
	.ascii	"ftl_memset\000"
.LASF534:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
.LASF45:
	.ascii	"crash_kexec_post_notifiers\000"
.LASF289:
	.ascii	"g_gc_bad_block_temp_num\000"
.LASF123:
	.ascii	"ftl_sys_save_info\000"
.LASF278:
	.ascii	"gp_last_act_superblock\000"
.LASF576:
	.ascii	"erase_flag\000"
.LASF487:
	.ascii	"blk_id\000"
.LASF337:
	.ascii	"ftl_gc_temp_block_bops_scan_page_addr\000"
