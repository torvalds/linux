/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-07-13
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
	.loc 2 1395 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL15:
	.loc 2 1398 0
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
	.loc 3 127 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL17:
	.loc 3 131 0
	movs	r1, #0
	.loc 3 129 0
	movs	r2, #1
.LVL18:
.L9:
	.loc 3 131 0 discriminator 1
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L10
	.loc 3 133 0
	subs	r0, r3, #1
.LVL19:
	.loc 3 135 0
	uxth	r0, r0
	bx	lr
.LVL20:
.L10:
	.loc 3 132 0 discriminator 3
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
.LBB222:
.LBB223:
	.loc 3 519 0
	ldr	r3, [r2, #124]
	ldrh	r2, [r2, #96]
	add	r2, r3, r2, lsl #1
.L29:
	.loc 3 518 0
	cmp	r3, r2
	bne	.L30
.L33:
.LBE223:
.LBE222:
	.loc 3 524 0
	movs	r0, #0
.LVL35:
	bx	lr
.LVL36:
.L30:
.LBB225:
.LBB224:
	.loc 3 519 0
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L29
	.loc 3 520 0
	movs	r0, #1
.LVL37:
.LBE224:
.LBE225:
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
	.loc 3 1214 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 3 1216 0
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
.LBB231:
.LBB232:
.LBB233:
	.loc 1 202 0
	movs	r2, #16
	movs	r1, #255
.LBE233:
.LBE232:
.LBE231:
	.loc 4 150 0
	strh	r3, [r0, #128]	@ movhi
	.loc 4 151 0
	movs	r3, #0
	strh	r3, [r0, #134]	@ movhi
.LBB236:
.LBB235:
.LBB234:
	.loc 1 202 0
	adds	r0, r0, #140
	b	memset
.LVL53:
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
.LBE234:
.LBE235:
.LBE236:
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
.LBB242:
.LBB243:
.LBB244:
	.loc 1 202 0
	mov	r1, #2048
.LBE244:
.LBE243:
.LBE242:
	.loc 2 76 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI9:
	.loc 2 79 0
	movs	r4, #0
	.loc 2 82 0
	strh	r0, [r3, #188]	@ movhi
.LBB249:
.LBB247:
.LBB245:
	.loc 1 202 0
	add	r0, r3, #196
.LVL75:
.LBE245:
.LBE247:
.LBE249:
	.loc 2 79 0
	strh	r4, [r3, #190]	@ movhi
	.loc 2 80 0
	strh	r4, [r3, #192]	@ movhi
	.loc 2 81 0
	strh	r4, [r3, #194]	@ movhi
.LBB250:
.LBB248:
.LBB246:
	.loc 1 202 0
	bl	__memzero
.LVL76:
.LBE246:
.LBE248:
.LBE250:
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
	.loc 2 118 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	.loc 2 123 0
	ldr	r3, .L88
	.loc 2 118 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI10:
	.loc 2 123 0
	ldrh	r2, [r3, #194]
	cbz	r2, .L79
	.loc 2 119 0
	ldrh	r5, [r3, #2272]
	movs	r0, #0
	ldrh	r1, [r3, #190]
	mov	r6, r0
	ldrh	r2, [r3, #192]
	and	r5, r5, #31
.LVL77:
.L81:
	.loc 2 124 0 discriminator 1
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L82
	cbz	r6, .L79
	strh	r1, [r3, #190]	@ movhi
	strh	r2, [r3, #192]	@ movhi
.L79:
	pop	{r4, r5, r6, pc}
.LVL78:
.L82:
	.loc 2 125 0 discriminator 3
	add	r4, r3, r1, lsl #1
	.loc 2 126 0 discriminator 3
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	.loc 2 125 0 discriminator 3
	ldrh	r6, [r4, #196]
.LVL79:
	.loc 2 127 0 discriminator 3
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #196]	@ movhi
	movs	r6, #1
.LVL80:
	.loc 2 128 0 discriminator 3
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L81
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
	.loc 2 163 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL81:
.LBB251:
.LBB252:
	.loc 2 94 0
	ldr	r3, .L96
.LBE252:
.LBE251:
	.loc 2 163 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI11:
.LBB254:
.LBB253:
	.loc 2 94 0
	ldrh	r4, [r3, #194]
.LBE253:
.LBE254:
	.loc 2 168 0
	cmp	r4, #1024
	beq	.L94
	.loc 2 171 0
	ldrh	r5, [r3, #190]
	movs	r1, #0
.LVL82:
.L92:
	.loc 2 170 0 discriminator 1
	cmp	r1, r4
	bcc	.L93
.L94:
	.loc 2 164 0
	movs	r0, #0
.LVL83:
	pop	{r4, r5, pc}
.LVL84:
.L93:
	.loc 2 171 0
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #196]
	cmp	r2, r0
	beq	.L95
	.loc 2 170 0 discriminator 2
	adds	r1, r1, #1
	b	.L92
.L95:
	.loc 2 172 0
	movs	r0, #1
.LVL85:
	.loc 2 178 0
	pop	{r4, r5, pc}
.L97:
	.align	2
.L96:
	.word	.LANCHOR0
.LFE192:
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
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
	.loc 2 249 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL86:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI12:
.LVL87:
	.loc 2 260 0
	ldr	r2, .L113
	ldrh	r3, [r2, #16]
	mov	r5, r2
.LVL88:
	cmp	r3, r0
	bls	.L100
.LBB257:
.LBB258:
	.loc 2 265 0
	movs	r4, #6
	ldr	ip, [r2, #2292]
	muls	r4, r0, r4
	.loc 2 266 0
	movw	r3, #65535
	.loc 2 265 0
	add	r1, ip, r4
.LVL89:
	.loc 2 266 0
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [ip, r4]	@ movhi
	.loc 2 268 0
	ldr	r3, [r2, #2296]
.LVL90:
	cbnz	r3, .L101
	.loc 2 269 0
	str	r1, [r2, #2296]
.LVL91:
.L100:
.LBE258:
.LBE257:
	.loc 2 323 0
	movs	r0, #0
.LVL92:
	add	sp, sp, #12
.LCFI13:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL93:
.L101:
.LCFI14:
.LBB260:
.LBB259:
	.loc 2 274 0
	ldrh	r6, [r1, #4]
	.loc 2 273 0
	lsl	r10, r0, #1
	ldr	r8, [r2, #2300]
	ldrh	r2, [r8, r0, lsl #1]
.LVL94:
	.loc 2 275 0
	cmp	r6, #0
	beq	.L111
	.loc 2 274 0
	muls	r6, r2, r6
.LVL95:
.L102:
	.loc 2 279 0
	ldr	lr, [r5, #2292]
	.loc 2 282 0
	ldrh	r7, [r5, #16]
	.loc 2 279 0
	sub	r2, r3, lr
.LVL96:
	asr	r9, r2, #1
	ldr	r2, .L113+4
	.loc 2 282 0
	str	r7, [sp]
	.loc 2 279 0
	mul	r2, r2, r9
	.loc 2 294 0
	ldr	r9, [r5, #2304]
	add	r5, r9, r10
	.loc 2 279 0
	uxth	r2, r2
	.loc 2 294 0
	str	r5, [sp, #4]
	.loc 2 256 0
	movs	r5, #0
.LVL97:
.L109:
	.loc 2 281 0
	adds	r5, r5, #1
.LVL98:
	.loc 2 282 0
	ldr	r7, [sp]
	.loc 2 281 0
	uxth	r5, r5
.LVL99:
	.loc 2 282 0
	cmp	r5, r7
	bhi	.L100
	.loc 2 285 0
	cmp	r0, r2
	beq	.L100
	.loc 2 289 0
	ldrh	r7, [r3, #4]
	.loc 2 288 0
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	.loc 2 290 0
	cbz	r7, .L112
	.loc 2 289 0
	mul	r7, r7, fp
.LVL100:
.L104:
	.loc 2 293 0
	cmp	r6, r7
	bne	.L105
	.loc 2 294 0
	ldr	r7, [sp, #4]
.LVL101:
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L107
.LVL102:
.L106:
	.loc 2 312 0
	strh	r2, [ip, r4]	@ movhi
	.loc 2 315 0
	ldr	r4, .L113
	.loc 2 313 0
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	.loc 2 315 0
	ldr	r2, [r4, #2296]
	cmp	r3, r2
	.loc 2 319 0
	ittte	ne
	ldrhne	r5, [r3, #2]
.LVL103:
	movne	r2, #6
	ldrne	r1, [r4, #2292]
.LVL104:
	.loc 2 316 0
	strheq	r0, [r3, #2]	@ movhi
.LVL105:
	.loc 2 317 0
	iteee	eq
	streq	r1, [r4, #2296]
	.loc 2 319 0
	mulne	r2, r2, r5
	strhne	r0, [r1, r2]	@ movhi
.LVL106:
	.loc 2 320 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L100
.LVL107:
.L111:
	.loc 2 276 0
	mov	r6, #-1
.LVL108:
	b	.L102
.LVL109:
.L112:
	.loc 2 291 0
	mov	r7, #-1
.LVL110:
	b	.L104
.L105:
	.loc 2 298 0
	bcc	.L106
.L107:
	.loc 2 301 0
	ldrh	r7, [r3]
.LVL111:
	movw	r10, #65535
	cmp	r7, r10
	bne	.L108
	.loc 2 302 0
	strh	r2, [r1, #2]	@ movhi
	.loc 2 303 0
	strh	r0, [r3]	@ movhi
	.loc 2 304 0
	ldr	r3, .L113
.LVL112:
	str	r1, [r3, #2308]
	b	.L100
.LVL113:
.L108:
	.loc 2 309 0
	movs	r3, #6
.LVL114:
	mov	r2, r7
	mla	r3, r3, r7, lr
.LVL115:
	b	.L109
.L114:
	.align	2
.L113:
	.word	.LANCHOR0
	.word	-1431655765
.LBE259:
.LBE260:
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
	.loc 2 218 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL116:
	push	{r3, lr}
	.save {r3, lr}
.LCFI15:
	.loc 2 219 0
	bl	insert_data_list
.LVL117:
	.loc 2 220 0
	ldr	r2, .L117
	ldrh	r3, [r2, #2312]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2312]	@ movhi
	.loc 2 221 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	bcs	.L115
.LBB263:
.LBB264:
	movs	r2, #221
	ldr	r1, .L117+4
	ldr	r0, .L117+8
.LBE264:
.LBE263:
	.loc 2 222 0
	pop	{r3, lr}
.LCFI16:
.LBB266:
.LBB265:
	.loc 2 221 0
	b	printk
.LVL118:
.L115:
.LCFI17:
	pop	{r3, pc}
.L118:
	.align	2
.L117:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC1
.LBE265:
.LBE266:
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
	.loc 2 326 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL119:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI18:
	.loc 2 332 0
	movw	r4, #65535
	cmp	r0, r4
	beq	.L120
	.loc 2 335 0
	ldr	r2, .L126
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2292]
	mov	r5, r2
	add	r6, ip, r7
.LVL120:
	.loc 2 336 0
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	.loc 2 338 0
	ldr	r3, [r2, #2316]
.LVL121:
	cbnz	r3, .L121
	.loc 2 339 0
	str	r6, [r2, #2316]
.LVL122:
.L120:
	.loc 2 372 0
	movs	r0, #0
.LVL123:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL124:
.L121:
	.loc 2 345 0
	ldr	lr, [r2, #2292]
	.loc 2 343 0
	ldr	r8, [r2, #2304]
	.loc 2 345 0
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L126+4
	.loc 2 343 0
	ldrh	r9, [r8, r0, lsl #1]
.LVL125:
	.loc 2 345 0
	mul	r2, r2, r10
	.loc 2 352 0
	mov	r10, r4
	.loc 2 345 0
	uxth	r2, r2
.L124:
	.loc 2 349 0
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L122
	.loc 2 352 0
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L123
	.loc 2 353 0
	strh	r2, [r6, #2]	@ movhi
	.loc 2 354 0
	strh	r0, [r3]	@ movhi
	.loc 2 355 0
	b	.L120
.L123:
	.loc 2 358 0
	mla	r3, r1, r4, lr
	.loc 2 348 0
	mov	r2, r4
	b	.L124
.L122:
	.loc 2 361 0
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	.loc 2 362 0
	strh	r2, [ip, r7]	@ movhi
	.loc 2 364 0
	ldr	r2, [r5, #2316]
	cmp	r3, r2
	.loc 2 368 0
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r5, #2292]
	.loc 2 365 0
	strheq	r0, [r3, #2]	@ movhi
	.loc 2 366 0
	iteee	eq
	streq	r6, [r5, #2316]
	.loc 2 368 0
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	.loc 2 369 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L120
.L127:
	.align	2
.L126:
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
	.loc 2 211 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL126:
	push	{r3, lr}
	.save {r3, lr}
.LCFI19:
	.loc 2 212 0
	bl	insert_free_list
.LVL127:
	.loc 2 213 0
	ldr	r2, .L130
	ldrh	r3, [r2, #2320]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2320]	@ movhi
	.loc 2 214 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	bcs	.L128
.LBB269:
.LBB270:
	movs	r2, #214
	ldr	r1, .L130+4
	ldr	r0, .L130+8
.LBE270:
.LBE269:
	.loc 2 215 0
	pop	{r3, lr}
.LCFI20:
.LBB272:
.LBB271:
	.loc 2 214 0
	b	printk
.LVL128:
.L128:
.LCFI21:
	pop	{r3, pc}
.L131:
	.align	2
.L130:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC1
.LBE271:
.LBE272:
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
	.loc 2 375 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL129:
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI22:
	.loc 2 378 0
	movs	r6, #6
	ldr	r4, .L138
	muls	r6, r1, r6
	.loc 2 379 0
	movw	r3, #65535
	.loc 2 375 0
	mov	r8, r0
	.loc 2 378 0
	ldr	r7, [r4, #2292]
	adds	r5, r7, r6
.LVL130:
	.loc 2 379 0
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L133
	.loc 2 379 0 is_stmt 0 discriminator 1
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L133
	.loc 2 379 0 discriminator 2
	movw	r2, #379
	ldr	r1, .L138+4
.LVL131:
	ldr	r0, .L138+8
	bl	printk
.LVL132:
.L133:
	.loc 2 381 0 is_stmt 1
	ldr	r3, [r8]
	movw	r1, #65535
.LVL133:
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L134
	.loc 2 382 0
	cmp	r3, r1
	.loc 2 385 0
	ittee	ne
	ldrne	r0, [r4, #2292]
	movne	r2, #6
	.loc 2 383 0
	moveq	r3, #0
	streq	r3, [r8]
	.loc 2 385 0
	ittt	ne
	mlane	r3, r2, r3, r0
	strne	r3, [r8]
	.loc 2 386 0
	strhne	r1, [r3, #2]	@ movhi
.L136:
	.loc 2 399 0
	movw	r3, #65535
	.loc 2 401 0
	movs	r0, #0
	.loc 2 399 0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	.loc 2 401 0
	pop	{r4, r5, r6, r7, r8, pc}
.LVL134:
.L134:
	.loc 2 388 0
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L137
	.loc 2 389 0
	cmp	r1, r3
	beq	.L136
	.loc 2 391 0
	movs	r2, #6
	ldr	r0, [r4, #2292]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L136
.L137:
	.loc 2 395 0
	ldr	r0, [r4, #2292]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	.loc 2 396 0
	ldrh	r0, [r5, #2]
	.loc 2 397 0
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2292]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L136
.L139:
	.align	2
.L138:
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
	.loc 2 404 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL135:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI23:
	.loc 2 408 0
	ldr	r3, [r0]
.LVL136:
	cbz	r3, .L146
	.loc 2 413 0
	ldr	r2, .L147
	.loc 2 412 0
	movw	r5, #65535
	.loc 2 413 0
	movs	r6, #6
	ldr	r2, [r2, #2292]
.L142:
	.loc 2 412 0
	cbnz	r1, .L143
.L145:
	.loc 2 416 0
	ldr	r4, .L147+4
	subs	r3, r3, r2
.LVL137:
	asrs	r3, r3, #1
	muls	r4, r3, r4
	.loc 2 417 0
	uxth	r1, r4
.LVL138:
	bl	List_remove_node
.LVL139:
	uxth	r0, r4
	.loc 2 419 0
	pop	{r4, r5, r6, pc}
.LVL140:
.L143:
	.loc 2 412 0 discriminator 1
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L145
	.loc 2 414 0
	subs	r1, r1, #1
.LVL141:
	.loc 2 413 0
	mla	r3, r6, r4, r2
	.loc 2 414 0
	uxth	r1, r1
.LVL142:
	b	.L142
.L146:
	.loc 2 409 0
	movw	r0, #65535
.LVL143:
	.loc 2 420 0
	pop	{r4, r5, r6, pc}
.L148:
	.align	2
.L147:
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
	.loc 2 423 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL144:
	.loc 2 424 0
	movs	r1, #0
	b	List_pop_index_node
.LVL145:
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
	.loc 2 428 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL146:
	.loc 2 432 0
	ldr	r2, .L156
	.loc 2 428 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI24:
	.loc 2 432 0
	ldr	r3, [r2, #2296]
.LVL147:
	cbz	r3, .L155
	.loc 2 437 0
	ldr	r1, [r2, #2292]
	movs	r4, #6
	.loc 2 436 0
	movw	r2, #65535
.L152:
	cbz	r0, .L153
	.loc 2 436 0 is_stmt 0 discriminator 1
	ldrh	r3, [r3]
.LVL148:
	cmp	r3, r2
	bne	.L154
.LVL149:
.L155:
	.loc 2 433 0 is_stmt 1
	movw	r0, #65535
.LVL150:
	pop	{r4, pc}
.LVL151:
.L154:
	.loc 2 438 0
	subs	r0, r0, #1
.LVL152:
	.loc 2 437 0
	mla	r3, r4, r3, r1
.LVL153:
	.loc 2 438 0
	uxth	r0, r0
.LVL154:
	b	.L152
.L153:
	.loc 2 442 0
	ldr	r0, .L156+4
.LVL155:
	subs	r3, r3, r1
.LVL156:
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	.loc 2 445 0
	pop	{r4, pc}
.L157:
	.align	2
.L156:
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
	.loc 2 448 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL157:
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
.LCFI25:
	.loc 2 448 0
	mov	r5, r0
	.loc 2 457 0
	ldr	r4, .L169
	ldrh	r3, [r4, #2324]
	cmp	r3, r0
	beq	.L160
	.loc 2 457 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2372]
	cmp	r3, r0
	beq	.L160
	.loc 2 457 0 discriminator 2
	ldrh	r3, [r4, #2420]
	cmp	r3, r0
	beq	.L160
.LBB275:
.LBB276:
	.loc 2 460 0 is_stmt 1
	movs	r7, #6
	ldr	r9, [r4, #2292]
	muls	r7, r0, r7
	.loc 2 462 0
	ldr	r3, [r4, #2296]
	.loc 2 460 0
	add	r8, r9, r7
.LVL158:
	.loc 2 462 0
	cmp	r8, r3
	beq	.L160
	.loc 2 466 0
	ldrh	r6, [r8, #4]
	.loc 2 465 0
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r0, lsl #1]
.LVL159:
	.loc 2 467 0
	cmp	r6, #0
	beq	.L167
	.loc 2 466 0
	muls	r6, r3, r6
.LVL160:
.L162:
	.loc 2 470 0
	ldrh	r3, [r8, #2]
.LVL161:
	movw	r2, #65535
	cmp	r3, r2
	bne	.L163
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L163
	mov	r2, #470
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	printk
.LVL162:
.L163:
	.loc 2 471 0
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L164
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L160
.L164:
	.loc 2 473 0
	movs	r2, #6
	muls	r2, r3, r2
	.loc 2 474 0
	ldr	r3, .L169+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	.loc 2 475 0
	ldr	r1, [r4, #2300]
	ldrh	r0, [r1, r3, lsl #1]
.LVL163:
	.loc 2 476 0
	ldr	r1, [r4, #2292]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	.loc 2 477 0
	cbz	r3, .L168
	.loc 2 476 0
	muls	r3, r0, r3
.LVL164:
.L165:
	.loc 2 480 0
	cmp	r6, r3
	bcs	.L160
	.loc 2 481 0
	mov	r1, r5
	ldr	r0, .L169+16
.LVL165:
	bl	List_remove_node
.LVL166:
	ldrh	r3, [r4, #2312]
	cbnz	r3, .L166
	movw	r2, #481
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	printk
.LVL167:
.L166:
	ldrh	r3, [r4, #2312]
	.loc 2 482 0
	mov	r0, r5
	.loc 2 481 0
	subs	r3, r3, #1
	strh	r3, [r4, #2312]	@ movhi
	.loc 2 482 0
	bl	INSERT_DATA_LIST
.LVL168:
.L160:
.LBE276:
.LBE275:
	.loc 2 485 0
	movs	r0, #0
.LVL169:
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.LVL170:
.L167:
.LBB278:
.LBB277:
	.loc 2 468 0
	mov	r6, #-1
.LVL171:
	b	.L162
.LVL172:
.L168:
	.loc 2 478 0
	mov	r3, #-1
.LVL173:
	b	.L165
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC1
	.word	-1431655765
	.word	.LANCHOR0+2296
.LBE277:
.LBE278:
.LFE202:
	.fnend
	.size	List_update_data_list, .-List_update_data_list
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
	.loc 2 749 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI26:
	.loc 2 754 0
	movs	r1, #0
	ldr	r5, .L181
	.loc 2 755 0
	movs	r0, #12
	movw	r6, #65535
	.loc 2 754 0
	ldrh	r2, [r5, #114]
	.loc 2 755 0
	ldr	r3, [r5, #2468]
.L172:
	uxth	r4, r1
	.loc 2 754 0 discriminator 1
	cmp	r4, r2
	bcc	.L174
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	.loc 2 765 0
	mov	ip, #12
.L175:
	uxth	r6, r1
	.loc 2 764 0 discriminator 1
	cmp	r6, r2
	bcc	.L177
	.loc 2 773 0
	cmp	r4, r2
	bcc	.L173
	.loc 2 779 0
	ldrh	r7, [r5, #2472]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L178:
	uxth	r5, r1
	.loc 2 778 0 discriminator 1
	cmp	r5, r2
	bcc	.L180
	.loc 2 784 0
	cmp	r4, r2
	bcc	.L173
	.loc 2 784 0 is_stmt 0 discriminator 1
	mov	r2, #784
	ldr	r1, .L181+4
	ldr	r0, .L181+8
	bl	printk
.LVL174:
	b	.L173
.L174:
	adds	r1, r1, #1
	.loc 2 755 0 is_stmt 1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L172
.L173:
	.loc 2 786 0
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L177:
	.loc 2 765 0
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L176
	.loc 2 766 0
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L176:
	adds	r1, r1, #1
	b	.L175
.L180:
	.loc 2 779 0
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L179
	.loc 2 779 0 is_stmt 0 discriminator 1
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L179:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L178
.L182:
	.align	2
.L181:
	.word	.LANCHOR0
	.word	.LANCHOR1+73
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
	.loc 2 931 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL175:
	.loc 2 934 0
	ldr	r1, .L189
	.loc 2 931 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI27:
	mov	r3, r1
	.loc 2 934 0
	ldrh	r2, [r1, #2474]
	cmp	r2, #4
	bhi	.L184
	.loc 2 934 0 is_stmt 0 discriminator 1
	cbnz	r0, .L184
	.loc 2 934 0
	adds	r2, r2, #1
	strh	r2, [r1, #2474]	@ movhi
	.loc 2 943 0 is_stmt 1
	pop	{r4, r5, pc}
.L184:
	.loc 2 935 0
	movs	r2, #0
	ldrh	r1, [r3, #16]
	strh	r2, [r3, #2474]	@ movhi
.LBB281:
.LBB282:
	.loc 2 939 0
	movw	r5, #65535
.LBE282:
.LBE281:
	.loc 2 936 0
	str	r2, [r3, #2476]
.LBB284:
.LBB283:
	.loc 2 939 0
	ldr	r2, [r3, #2300]
	add	r1, r2, r1, lsl #1
.L185:
	.loc 2 937 0
	cmp	r2, r1
	bne	.L187
	pop	{r4, r5, pc}
.L187:
	.loc 2 939 0
	ldrh	r4, [r2], #2
	cmp	r4, r5
	.loc 2 940 0
	ittt	ne
	ldrne	r0, [r3, #2476]
.LVL176:
	addne	r0, r0, r4
	strne	r0, [r3, #2476]
	b	.L185
.L190:
	.align	2
.L189:
	.word	.LANCHOR0
.LBE283:
.LBE284:
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
	.loc 2 1396 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL177:
	.loc 2 1396 0
	mov	r3, r0
	.loc 2 1397 0
	cbz	r1, .L192
	b	ftl_set_blk_mode.part.6
.LVL178:
.L192:
	.loc 2 1400 0
	ldr	r2, .L193
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
.L194:
	.align	2
.L193:
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
	.loc 2 1404 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL181:
	.loc 2 1405 0
	ldr	r3, .L196
	lsrs	r2, r0, #5
	and	r0, r0, #31
.LVL182:
	ldr	r3, [r3]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	.loc 2 1406 0
	and	r0, r0, #1
	bx	lr
.L197:
	.align	2
.L196:
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
	.loc 2 1657 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL183:
	.loc 2 1659 0
	movs	r3, #0
	.loc 2 1657 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
.LCFI28:
	.loc 2 1659 0
	strh	r3, [r0, #4]	@ movhi
	add	r4, r0, r2, lsl #1
	.loc 2 1662 0
	movw	r7, #65535
	.loc 2 1660 0
	ldr	r3, .L205
	adds	r4, r4, #14
	ldrh	r5, [r3, #8]
.L199:
	.loc 2 1660 0 is_stmt 0 discriminator 1
	cmp	r2, r5
	bcc	.L201
	ldrh	r3, [r3, #78]
	add	r4, r0, #16
	.loc 2 1669 0 is_stmt 1
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
.LVL184:
	.loc 2 1671 0
	movs	r3, #0
	uxth	r1, r1
.L202:
	.loc 2 1667 0 discriminator 1
	uxth	r2, r3
.LVL185:
	cmp	r5, r2
	bhi	.L204
	.loc 2 1674 0
	pop	{r4, r5, r6, r7, pc}
.LVL186:
.L201:
	.loc 2 1662 0
	ldrh	r6, [r4, #2]!
	.loc 2 1660 0
	adds	r2, r2, #1
.LVL187:
	uxth	r2, r2
.LVL188:
	.loc 2 1662 0
	cmp	r6, r7
	.loc 2 1664 0
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L199
.LVL189:
.L204:
	.loc 2 1669 0
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	.loc 2 1671 0
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L202
.L206:
	.align	2
.L205:
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
	.loc 2 2062 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL190:
	.loc 2 2065 0
	ldrh	r3, [r0, #4]
	.loc 2 2062 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI29:
	.loc 2 2065 0
	cbz	r3, .L207
	.loc 2 2067 0
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L207
.LBB287:
.LBB288:
	.loc 2 2069 0
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	.loc 2 2074 0
	movs	r5, #0
	.loc 2 2069 0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
.LVL191:
	.loc 2 2072 0
	ldr	r2, .L213
	ldrh	r4, [r2, #8]
	.loc 2 2070 0
	mov	r2, r3
.L210:
	cmp	r1, r2
	beq	.L212
.L207:
	pop	{r4, r5, pc}
.L212:
	.loc 2 2071 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2072 0
	cmp	r3, r4
	.loc 2 2071 0
	strb	r3, [r0, #6]
	.loc 2 2073 0
	itttt	eq
	ldrheq	r3, [r0, #2]
	.loc 2 2074 0
	strbeq	r5, [r0, #6]
	.loc 2 2073 0
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	.loc 2 2076 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r1, [r0, r3, lsl #1]
	b	.L210
.L214:
	.align	2
.L213:
	.word	.LANCHOR0
.LBE288:
.LBE287:
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
	.loc 2 2094 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL192:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI30:
	.loc 2 2094 0
	mov	r4, r0
	.loc 2 2097 0
	ldr	r6, .L220
	ldrh	r2, [r0]
	ldrh	r3, [r6, #16]
	cmp	r2, r3
	bcc	.L216
	.loc 2 2097 0 is_stmt 0 discriminator 1
	movw	r2, #2097
	ldr	r1, .L220+4
	ldr	r0, .L220+8
	bl	printk
.LVL193:
.L216:
	.loc 2 2100 0 is_stmt 1
	ldrh	r9, [r6, #8]
	add	r7, r4, #16
	.loc 2 2101 0
	ldr	r10, .L220+12
	.loc 2 2102 0
	movw	r8, #65535
	.loc 2 2098 0
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	.loc 2 2099 0
	strb	r5, [r4, #7]
.L217:
	.loc 2 2100 0 discriminator 1
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L219
	.loc 2 2110 0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	.loc 2 2113 0
	movs	r0, #0
	.loc 2 2110 0
	ldrh	r2, [r6, #78]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2111 0
	movs	r3, #1
	strb	r3, [r4, #9]
	.loc 2 2113 0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL194:
.L219:
	.loc 2 2101 0
	ldrh	r1, [r4]
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL195:
	.loc 2 2102 0
	strh	r8, [r7]	@ movhi
	.loc 2 2101 0
	mov	fp, r0
	.loc 2 2103 0
	bl	FtlBbmIsBadBlock
.LVL196:
	cbnz	r0, .L218
	.loc 2 2104 0
	strh	fp, [r7]	@ movhi
	.loc 2 2105 0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L218:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L217
.L221:
	.align	2
.L220:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
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
	.loc 2 2451 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL197:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI31:
	.loc 2 2455 0
	movs	r5, #0
	ldr	r6, .L228
	.loc 2 2451 0
	mov	r7, r0
	.loc 2 2453 0
	mov	r4, r5
	.loc 2 2455 0
	ldrh	r8, [r6, #8]
	.loc 2 2456 0
	add	r10, r6, #36
	.loc 2 2458 0
	ldrh	r9, [r6, #78]
.L223:
	.loc 2 2455 0 discriminator 1
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L225
	.loc 2 2460 0
	cbz	r4, .L227
	.loc 2 2461 0
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.LVL198:
.L226:
	.loc 2 2462 0
	ldr	r1, [r6, #2292]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	.loc 2 2464 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL199:
.L225:
	.loc 2 2456 0
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL200:
	.loc 2 2457 0
	bl	FtlBbmIsBadBlock
.LVL201:
	cbnz	r0, .L224
	.loc 2 2458 0
	add	r4, r4, r9
	uxth	r4, r4
.L224:
	adds	r5, r5, #1
	b	.L223
.L227:
	mov	r0, r4
	b	.L226
.L229:
	.align	2
.L228:
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
	.loc 2 2467 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 2 2470 0
	ldr	r2, .L233
	ldr	r0, [r2, #2316]
	cbz	r0, .L231
	.loc 2 2471 0
	ldr	r3, [r2, #2292]
	subs	r0, r0, r3
	ldr	r3, .L233+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2304]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L231:
	.loc 2 2473 0
	bx	lr
.L234:
	.align	2
.L233:
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
	.loc 2 2476 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL202:
	.loc 2 2481 0
	ldr	r1, .L243
	.loc 2 2476 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
.LCFI32:
	.loc 2 2481 0
	ldr	r3, [r1, #2316]
	cbz	r3, .L241
	.loc 2 2482 0
	ldrh	r2, [r1, #2320]
	.loc 2 2486 0
	movs	r6, #6
	.loc 2 2484 0
	ldr	r4, [r1, #2292]
	.loc 2 2486 0
	movw	r7, #65535
	.loc 2 2482 0
	rsb	r2, r2, r2, lsl #3
	.loc 2 2484 0
	subs	r3, r3, r4
	asrs	r3, r3, #1
	.loc 2 2482 0
	asrs	r2, r2, #3
	cmp	r0, r2
	.loc 2 2483 0
	it	gt
	uxthgt	r0, r2
	.loc 2 2484 0
	ldr	r2, .L243+4
	muls	r3, r2, r3
	.loc 2 2485 0
	movs	r2, #0
	.loc 2 2484 0
	uxth	r3, r3
.L238:
	.loc 2 2485 0 discriminator 1
	uxth	r5, r2
	cmp	r0, r5
	bls	.L240
	.loc 2 2486 0
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L242
.L240:
	.loc 2 2490 0
	ldr	r2, [r1, #2304]
	ldrh	r0, [r2, r3, lsl #1]
.LVL203:
	pop	{r4, r5, r6, r7, pc}
.LVL204:
.L242:
	mov	r3, r5
	b	.L238
.L241:
	.loc 2 2477 0
	mov	r0, r3
.LVL205:
	.loc 2 2493 0
	pop	{r4, r5, r6, r7, pc}
.L244:
	.align	2
.L243:
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
.LCFI33:
	mov	r7, r0
	.loc 3 16 0
	ldr	r6, .L257
	.loc 3 11 0
	.pad #20
	sub	sp, sp, #20
.LCFI34:
	.loc 3 15 0
	add	r5, r7, #12
	ldr	r1, .L257+4
	bl	strcpy
.LVL207:
	.loc 3 16 0
	ldr	r2, [r6, #92]
	mov	r0, r5
	ldr	r1, .L257+8
	bl	sprintf
.LVL208:
	add	r5, r5, r0
	.loc 3 17 0
	ldr	r1, .L257+12
	mov	r0, r5
	adds	r5, r5, #10
	bl	strcpy
.LVL209:
	.loc 3 18 0
	ldr	r2, [r6, #2480]
	mov	r0, r5
	ldr	r1, .L257+16
	bl	sprintf
.LVL210:
	add	r5, r5, r0
	.loc 3 19 0
	ldr	r2, [r6, #2476]
	ldr	r1, .L257+20
	mov	r0, r5
	bl	sprintf
.LVL211:
	add	r5, r5, r0
	.loc 3 20 0
	ldr	r2, [r6, #2484]
	ldr	r1, .L257+24
	mov	r0, r5
	bl	sprintf
.LVL212:
	add	r5, r5, r0
	.loc 3 21 0
	ldr	r2, [r6, #2488]
	ldr	r1, .L257+28
	mov	r0, r5
	bl	sprintf
.LVL213:
	add	r5, r5, r0
	.loc 3 22 0
	ldr	r2, [r6, #2492]
	ldr	r1, .L257+32
	mov	r0, r5
	bl	sprintf
.LVL214:
	add	r5, r5, r0
	.loc 3 23 0
	ldr	r2, [r6, #2496]
	ldr	r1, .L257+36
	mov	r0, r5
	bl	sprintf
.LVL215:
	add	r5, r5, r0
	.loc 3 24 0
	ldr	r2, [r6, #2500]
	ldr	r1, .L257+40
	mov	r0, r5
	bl	sprintf
.LVL216:
	add	r5, r5, r0
	.loc 3 25 0
	ldr	r2, [r6, #2504]
	ldr	r1, .L257+44
	mov	r0, r5
	bl	sprintf
.LVL217:
	.loc 3 26 0
	ldr	r2, [r6, #2508]
	.loc 3 25 0
	add	r5, r5, r0
	.loc 3 26 0
	ldr	r1, .L257+48
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
.LVL218:
	.loc 3 27 0
	ldr	r2, [r6, #2512]
	.loc 3 26 0
	add	r5, r5, r0
	.loc 3 27 0
	ldr	r1, .L257+52
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
.LVL219:
	add	r5, r5, r0
	.loc 3 28 0
	ldr	r2, [r6, #2516]
	ldr	r1, .L257+56
	mov	r0, r5
	bl	sprintf
.LVL220:
	add	r5, r5, r0
	.loc 3 29 0
	ldr	r2, [r6, #2520]
	ldr	r1, .L257+60
	mov	r0, r5
	bl	sprintf
.LVL221:
	add	r5, r5, r0
	.loc 3 30 0
	ldrh	r2, [r6, #134]
	ldr	r1, .L257+64
	mov	r0, r5
	bl	sprintf
.LVL222:
	add	r5, r5, r0
	.loc 3 31 0
	ldrh	r2, [r6, #2320]
	ldr	r1, .L257+68
	mov	r0, r5
	bl	sprintf
.LVL223:
	add	r5, r5, r0
	.loc 3 32 0
	ldr	r2, [r6, #2524]
	ldr	r1, .L257+72
	mov	r0, r5
	bl	sprintf
.LVL224:
	add	r5, r5, r0
	.loc 3 33 0
	ldr	r2, [r6, #2528]
	ldr	r1, .L257+76
	mov	r0, r5
	bl	sprintf
.LVL225:
	add	r5, r5, r0
	.loc 3 34 0
	ldr	r2, [r6, #2532]
	ldr	r1, .L257+80
	mov	r0, r5
	bl	sprintf
.LVL226:
	add	r5, r5, r0
	.loc 3 35 0
	ldr	r2, [r6, #2536]
	ldr	r1, .L257+84
	mov	r0, r5
	bl	sprintf
.LVL227:
	add	r5, r5, r0
	.loc 3 36 0
	ldr	r2, [r6, #2540]
	ldr	r1, .L257+88
	mov	r0, r5
	bl	sprintf
.LVL228:
	add	r5, r5, r0
	.loc 3 37 0
	ldr	r2, [r6, #2544]
	ldr	r1, .L257+92
	mov	r0, r5
	bl	sprintf
.LVL229:
	add	r5, r5, r0
	.loc 3 38 0
	ldrh	r2, [r6, #2274]
	ldr	r1, .L257+96
	mov	r0, r5
	bl	sprintf
.LVL230:
	add	r5, r5, r0
	.loc 3 39 0
	ldrh	r2, [r6, #2272]
	ldr	r1, .L257+100
	mov	r0, r5
	bl	sprintf
.LVL231:
	add	r5, r5, r0
	.loc 3 40 0
	ldr	r2, [r6, #116]
	ldr	r1, .L257+104
	mov	r0, r5
	bl	sprintf
.LVL232:
	add	r5, r5, r0
	.loc 3 41 0
	ldr	r2, [r6, #108]
	ldr	r1, .L257+108
	mov	r0, r5
	bl	sprintf
.LVL233:
	add	r5, r5, r0
	.loc 3 42 0
	ldr	r2, [r6, #4]
	ldr	r1, .L257+112
	mov	r0, r5
	bl	sprintf
.LVL234:
	add	r5, r5, r0
	.loc 3 43 0
	ldrh	r2, [r6, #194]
	ldr	r1, .L257+116
	mov	r0, r5
	bl	sprintf
.LVL235:
	add	r5, r5, r0
	.loc 3 44 0
	ldrh	r2, [r6, #16]
	ldr	r1, .L257+120
	mov	r0, r5
	bl	sprintf
.LVL236:
	add	r5, r5, r0
	.loc 3 45 0
	ldrh	r2, [r6, #2548]
	ldr	r1, .L257+124
	mov	r0, r5
	bl	sprintf
.LVL237:
	add	r5, r5, r0
	.loc 3 46 0
	ldr	r2, [r6, #20]
	ldr	r1, .L257+128
	mov	r0, r5
	bl	sprintf
.LVL238:
	add	r5, r5, r0
	.loc 3 47 0
	ldrh	r2, [r6, #2552]
	ldr	r1, .L257+132
	mov	r0, r5
	bl	sprintf
.LVL239:
	add	r5, r5, r0
	.loc 3 48 0
	ldrh	r2, [r6, #128]
	ldr	r1, .L257+136
	mov	r0, r5
	bl	sprintf
.LVL240:
	add	r5, r5, r0
	.loc 3 49 0
	ldrh	r2, [r6, #2326]
	ldr	r1, .L257+140
	mov	r0, r5
	bl	sprintf
.LVL241:
	add	r5, r5, r0
	.loc 3 50 0
	ldrb	r2, [r6, #2330]	@ zero_extendqisi2
	ldr	r1, .L257+144
	mov	r0, r5
	bl	sprintf
.LVL242:
	add	r5, r5, r0
	.loc 3 51 0
	ldrh	r2, [r6, #2324]
	ldr	r1, .L257+148
	mov	r0, r5
	bl	sprintf
.LVL243:
	add	r5, r5, r0
	.loc 3 52 0
	ldrb	r2, [r6, #2332]	@ zero_extendqisi2
	ldr	r1, .L257+152
	mov	r0, r5
	bl	sprintf
.LVL244:
	add	r5, r5, r0
	.loc 3 53 0
	ldrh	r2, [r6, #2328]
	ldr	r1, .L257+156
	mov	r0, r5
	bl	sprintf
.LVL245:
	.loc 3 54 0
	ldr	r3, [r6, #2300]
	.loc 3 53 0
	add	r5, r5, r0
	.loc 3 54 0
	ldrh	r2, [r6, #2324]
	mov	r0, r5
	ldr	r1, .L257+160
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
.LVL246:
	add	r5, r5, r0
	.loc 3 55 0
	ldrh	r2, [r6, #2374]
	ldr	r1, .L257+164
	mov	r0, r5
	bl	sprintf
.LVL247:
	add	r5, r5, r0
	.loc 3 56 0
	ldrb	r2, [r6, #2378]	@ zero_extendqisi2
	ldr	r1, .L257+168
	mov	r0, r5
	bl	sprintf
.LVL248:
	add	r5, r5, r0
	.loc 3 57 0
	ldrh	r2, [r6, #2372]
	ldr	r1, .L257+172
	mov	r0, r5
	bl	sprintf
.LVL249:
	add	r5, r5, r0
	.loc 3 58 0
	ldrb	r2, [r6, #2380]	@ zero_extendqisi2
	ldr	r1, .L257+176
	mov	r0, r5
	bl	sprintf
.LVL250:
	add	r5, r5, r0
	.loc 3 59 0
	ldrh	r2, [r6, #2376]
	ldr	r1, .L257+180
	mov	r0, r5
	bl	sprintf
.LVL251:
	.loc 3 60 0
	ldr	r3, [r6, #2300]
	.loc 3 59 0
	add	r5, r5, r0
	.loc 3 60 0
	ldrh	r2, [r6, #2372]
	mov	r0, r5
	ldr	r1, .L257+184
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
.LVL252:
	add	r5, r5, r0
	.loc 3 61 0
	ldrh	r2, [r6, #2422]
	ldr	r1, .L257+188
	mov	r0, r5
	bl	sprintf
.LVL253:
	add	r5, r5, r0
	.loc 3 62 0
	ldrb	r2, [r6, #2426]	@ zero_extendqisi2
	ldr	r1, .L257+192
	mov	r0, r5
	bl	sprintf
.LVL254:
	add	r5, r5, r0
	.loc 3 63 0
	ldrh	r2, [r6, #2420]
	ldr	r1, .L257+196
	mov	r0, r5
	bl	sprintf
.LVL255:
	add	r5, r5, r0
	.loc 3 64 0
	ldrb	r2, [r6, #2428]	@ zero_extendqisi2
	ldr	r1, .L257+200
	mov	r0, r5
	bl	sprintf
.LVL256:
	add	r5, r5, r0
	.loc 3 65 0
	ldrh	r2, [r6, #2424]
	ldr	r1, .L257+204
	mov	r0, r5
	bl	sprintf
.LVL257:
	add	r5, r5, r0
	.loc 3 66 0
	ldrh	r2, [r6, #2566]
	ldr	r1, .L257+208
	mov	r0, r5
	bl	sprintf
.LVL258:
	add	r5, r5, r0
	.loc 3 67 0
	ldrb	r2, [r6, #2570]	@ zero_extendqisi2
	ldr	r1, .L257+212
	mov	r0, r5
	bl	sprintf
.LVL259:
	add	r5, r5, r0
	.loc 3 68 0
	ldrh	r2, [r6, #2564]
	ldr	r1, .L257+216
	mov	r0, r5
	bl	sprintf
.LVL260:
	add	r5, r5, r0
	.loc 3 69 0
	ldrb	r2, [r6, #2572]	@ zero_extendqisi2
	ldr	r1, .L257+220
	mov	r0, r5
	bl	sprintf
.LVL261:
	add	r5, r5, r0
	.loc 3 70 0
	ldrh	r2, [r6, #2568]
	ldr	r1, .L257+224
	mov	r0, r5
	bl	sprintf
.LVL262:
	.loc 3 71 0
	ldr	r3, [r6, #2688]
	.loc 3 70 0
	add	r5, r5, r0
	.loc 3 71 0
	ldr	r2, [r6, #2692]
	mov	r0, r5
	ldr	r1, .L257+228
	str	r3, [sp]
	ldr	r3, [r6, #2696]
	bl	sprintf
.LVL263:
	adds	r4, r5, r0
	.loc 3 72 0
	ldr	r2, [r6, #2684]
	ldr	r1, .L257+232
	mov	r0, r4
	bl	sprintf
.LVL264:
	add	r4, r4, r0
	.loc 3 73 0
	ldr	r2, [r6, #2708]
	ldr	r1, .L257+236
	mov	r0, r4
	bl	sprintf
.LVL265:
	add	r4, r4, r0
	.loc 3 74 0
	ldrh	r2, [r6, #3124]
	ldr	r1, .L257+240
	mov	r0, r4
	bl	sprintf
.LVL266:
	add	r4, r4, r0
	.loc 3 75 0
	ldrh	r2, [r6, #3126]
	ldr	r1, .L257+244
	mov	r0, r4
	bl	sprintf
.LVL267:
	add	r4, r4, r0
	.loc 3 76 0
	ldr	r2, [r6, #3128]
	b	.L258
.L259:
	.align	2
.L257:
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
.L258:
	ldr	r1, .L260
	mov	r0, r4
	bl	sprintf
.LVL268:
	add	r4, r4, r0
	.loc 3 77 0
	ldrh	r2, [r6, #3132]
	ldr	r1, .L260+4
	mov	r0, r4
	bl	sprintf
.LVL269:
	add	r4, r4, r0
	.loc 3 78 0
	bl	GetFreeBlockMinEraseCount
.LVL270:
	ldr	r1, .L260+8
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL271:
	add	r4, r4, r0
	.loc 3 79 0
	ldrh	r0, [r6, #2320]
	bl	GetFreeBlockMaxEraseCount
.LVL272:
	ldr	r1, .L260+12
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL273:
	.loc 3 80 0
	ldr	r3, .L260+16
	.loc 3 79 0
	add	r4, r4, r0
	.loc 3 80 0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L246
.LVL274:
.L251:
	.loc 3 103 0
	subs	r0, r4, r7
	.loc 3 104 0
	add	sp, sp, #20
.LCFI35:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL275:
.L246:
.LCFI36:
	.loc 3 83 0
	ldrh	r3, [r6, #2564]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L248
	.loc 3 84 0
	ldr	r2, [r6, #2300]
	mov	r0, r4
	ldr	r1, .L260+20
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
.LVL276:
	add	r4, r4, r0
.L248:
.LBB289:
	.loc 3 86 0
	movs	r0, #0
	.loc 3 90 0
	ldr	r9, .L260+28
	.loc 3 86 0
	bl	List_get_gc_head_node
.LVL277:
	uxth	r3, r0
.LVL278:
	.loc 3 87 0
	movs	r5, #0
	.loc 3 88 0
	movw	fp, #65535
	.loc 3 90 0
	mov	r10, #6
.L250:
	.loc 3 88 0
	cmp	r3, fp
	beq	.L249
	.loc 3 90 0 discriminator 2
	ldr	r2, [r6, #2304]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, r9
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r6, #2292]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2300]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
.LVL279:
	.loc 3 87 0 discriminator 2
	adds	r5, r5, #1
	.loc 3 91 0 discriminator 2
	ldr	r3, [r6, #2292]
	.loc 3 87 0 discriminator 2
	cmp	r5, #16
	.loc 3 90 0 discriminator 2
	add	r4, r4, r0
	.loc 3 91 0 discriminator 2
	ldrh	r3, [r3, r8]
.LVL280:
	.loc 3 87 0 discriminator 2
	bne	.L250
.L249:
.LBE289:
.LBB290:
	.loc 3 95 0
	ldr	r2, [r6, #2292]
	.loc 3 96 0
	movs	r5, #0
	.loc 3 95 0
	ldr	r3, [r6, #2316]
.LVL281:
	.loc 3 97 0
	movw	r9, #65535
	.loc 3 99 0
	ldr	fp, .L260+32
	mov	r10, #6
	.loc 3 95 0
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L260+24
	muls	r3, r2, r3
	uxth	r3, r3
.LVL282:
.L252:
	.loc 3 97 0
	cmp	r3, r9
	beq	.L251
	.loc 3 99 0 discriminator 2
	ldr	r2, [r6, #2304]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, fp
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2292]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	.loc 3 96 0 discriminator 2
	adds	r5, r5, #1
	.loc 3 99 0 discriminator 2
	bl	sprintf
.LVL283:
	.loc 3 96 0 discriminator 2
	cmp	r5, #4
	.loc 3 99 0 discriminator 2
	add	r4, r4, r0
	.loc 3 96 0 discriminator 2
	beq	.L251
	.loc 3 100 0
	ldr	r3, [r6, #2292]
	ldrh	r3, [r3, r8]
.LVL284:
	b	.L252
.L261:
	.align	2
.L260:
	.word	.LC63
	.word	.LC64
	.word	.LC65
	.word	.LC66
	.word	.LANCHOR2
	.word	.LC67
	.word	-1431655765
	.word	.LC68
	.word	.LC69
.LBE290:
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
	.loc 3 113 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL285:
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI37:
	.loc 3 113 0
	mov	r5, r0
	.loc 3 119 0
	ldr	r2, .L263
	ldr	r1, .L263+4
	bl	sprintf
.LVL286:
	adds	r4, r5, r0
	.loc 3 121 0
	mov	r0, r4
	bl	FtlPrintInfo2buf
.LVL287:
	add	r0, r0, r4
	.loc 3 123 0
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.LVL288:
.L264:
	.align	2
.L263:
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
	.loc 2 2497 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI38:
	.pad #28
	sub	sp, sp, #28
.LCFI39:
	.loc 2 2507 0
	ldr	r4, .L289
	ldr	r2, [r4, #2532]
	ldr	r3, [r4, #2544]
	cmp	r2, r3
	bcs	.L266
	.loc 2 2510 0
	ldrh	r1, [r4, #16]
	.loc 2 2509 0
	movs	r3, #0
	.loc 2 2511 0
	ldr	r0, [r4, #2304]
	.loc 2 2509 0
	str	r3, [r4, #2524]
.L267:
	.loc 2 2510 0 discriminator 1
	cmp	r3, r1
	bcc	.L268
	.loc 2 2512 0
	ldr	r5, [r4, #2524]
	mov	r0, r5
	bl	__aeabi_uidiv
.LVL289:
	str	r0, [r4, #2532]
	.loc 2 2513 0
	ldr	r0, [r4, #2528]
	ldrh	r1, [r4, #68]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
.LVL290:
	str	r0, [r4, #2524]
.L269:
	.loc 2 2519 0
	ldr	r6, [r4, #2544]
	ldr	r7, [r4, #2532]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L274
	.loc 2 2519 0 is_stmt 0 discriminator 1
	ldr	r2, [r4, #2540]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L274
.LVL291:
.L276:
	.loc 2 2520 0 is_stmt 1
	movw	r5, #65535
.L275:
	.loc 2 2566 0
	mov	r0, r5
.LVL292:
	add	sp, sp, #28
.LCFI40:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL293:
.L268:
.LCFI41:
	.loc 2 2511 0 discriminator 3
	ldrh	r2, [r0, r3, lsl #1]
	.loc 2 2510 0 discriminator 3
	adds	r3, r3, #1
	.loc 2 2511 0 discriminator 3
	ldr	r5, [r4, #2524]
	add	r2, r2, r5
	str	r2, [r4, #2524]
	b	.L267
.L266:
	.loc 2 2514 0
	ldr	r3, [r4, #2540]
	cmp	r2, r3
	bls	.L269
	.loc 2 2515 0
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	.loc 2 2516 0
	movs	r3, #0
.L271:
	.loc 2 2516 0 is_stmt 0 discriminator 1
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	bcs	.L269
	.loc 2 2517 0 is_stmt 1 discriminator 3
	ldr	r1, [r4, #2304]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	.loc 2 2516 0 discriminator 3
	adds	r3, r3, #1
	b	.L271
.L274:
	.loc 2 2522 0
	ldrh	r0, [r4, #2320]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
.LVL294:
	.loc 2 2523 0
	add	r3, r6, #64
	.loc 2 2522 0
	mov	r9, r0
.LVL295:
	.loc 2 2523 0
	cmp	r0, r3
	bcc	.L276
	.loc 2 2526 0
	ldr	r3, [r4, #2296]
.LVL296:
	cmp	r3, #0
	beq	.L276
	.loc 2 2530 0
	ldrh	r2, [r4, #16]
	.loc 2 2534 0
	movw	r1, #65535
.LVL297:
	.loc 2 2533 0
	ldr	lr, [r4, #2292]
	.loc 2 2534 0
	movs	r0, #0
.LVL298:
	ldr	r8, [r4, #2304]
	.loc 2 2528 0
	mov	r10, r1
	.loc 2 2530 0
	str	r2, [sp, #20]
	.loc 2 2543 0
	mov	fp, #6
	.loc 2 2534 0
	mov	r2, r1
.L277:
	.loc 2 2528 0
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L280
	mov	r5, r2
.L279:
	.loc 2 2546 0
	movw	r3, #65535
.LVL299:
	cmp	r5, r3
	beq	.L276
	.loc 2 2548 0
	ldrh	r10, [r8, r5, lsl #1]
.LVL300:
	lsl	fp, r5, #1
	.loc 2 2549 0
	cmp	r6, r10
	bcs	.L281
	.loc 2 2551 0
	bl	GetFreeBlockMinEraseCount
.LVL301:
	cmp	r6, r0
	.loc 2 2552 0
	it	cc
	strcc	r1, [r4, #2544]
.LVL302:
.L281:
	.loc 2 2554 0
	cmp	r7, r10
	bls	.L276
	.loc 2 2554 0 is_stmt 0 discriminator 1
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L276
	.loc 2 2556 0 is_stmt 1
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L282
	.loc 2 2556 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #2540]
	add	r10, r10, #768
.LVL303:
	cmp	r10, r3
	bcs	.L276
.LVL304:
.L282:
	.loc 2 2558 0 is_stmt 1
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
.LVL305:
	ldr	r0, .L289+4
.LVL306:
	str	r3, [sp, #4]
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2540]
	bl	printk
.LVL307:
	.loc 2 2559 0
	movs	r3, #1
	str	r3, [r4, #3136]
	.loc 2 2560 0
	b	.L275
.LVL308:
.L280:
	.loc 2 2529 0
	adds	r0, r0, #1
.LVL309:
	.loc 2 2530 0
	ldr	r5, [sp, #20]
	.loc 2 2529 0
	uxth	r0, r0
.LVL310:
	.loc 2 2530 0
	cmp	r0, r5
	bhi	.L276
	.loc 2 2532 0
	ldrh	r5, [r3, #4]
	cbz	r5, .L278
	.loc 2 2533 0
	ldr	r5, .L289+8
	sub	r3, r3, lr
.LVL311:
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	.loc 2 2534 0
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L279
	.loc 2 2538 0
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	.loc 2 2533 0
	movhi	r2, r5
.LVL312:
.L278:
	.loc 2 2543 0
	mla	r3, fp, ip, lr
	b	.L277
.L290:
	.align	2
.L289:
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
	.loc 2 2570 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL313:
	.loc 2 2571 0
	movw	r2, #65535
	.loc 2 2570 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI42:
	.loc 2 2571 0
	cmp	r0, r2
	beq	.L292
.LBB293:
.LBB294:
	.loc 2 2573 0
	ldr	r2, .L293
	movs	r1, #0
	ldr	r2, [r2, #2300]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	.loc 2 2574 0
	bl	INSERT_FREE_LIST
.LVL314:
.L292:
.LBE294:
.LBE293:
	.loc 2 2576 0
	movs	r0, #0
.LVL315:
	pop	{r3, pc}
.L294:
	.align	2
.L293:
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
	.loc 2 2703 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL316:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI43:
	.loc 2 2707 0
	movw	r3, #65535
	ldrh	r2, [r0]
	.loc 2 2703 0
	mov	r4, r0
	.loc 2 2707 0
	cmp	r2, r3
	bne	.L296
	.loc 2 2707 0 is_stmt 0 discriminator 1
	movw	r2, #2707
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.LVL317:
.L296:
	.loc 2 2708 0 is_stmt 1
	ldr	r5, .L309+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #78]
	cmp	r2, r3
	bne	.L297
	.loc 2 2708 0 is_stmt 0 discriminator 1
	movw	r2, #2708
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.LVL318:
.L297:
	.loc 2 2709 0 is_stmt 1
	ldrh	r3, [r4, #4]
	cbnz	r3, .L298
	.loc 2 2709 0 is_stmt 0 discriminator 1
	movw	r2, #2709
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.LVL319:
.L298:
	.loc 2 2711 0 is_stmt 1
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	.loc 2 2710 0
	movs	r3, #0
	.loc 2 2714 0
	ldrh	r1, [r5, #8]
	.loc 2 2712 0
	movw	r6, #65535
	.loc 2 2710 0
	strb	r3, [r4, #10]
	.loc 2 2711 0
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
.LVL320:
	.loc 2 2716 0
	mov	r2, r3
.L299:
	.loc 2 2712 0
	cmp	r0, r6
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L301
	.loc 2 2721 0
	ldrh	r2, [r4, #4]
	.loc 2 2720 0
	ldrh	r6, [r4, #2]
	.loc 2 2721 0
	subs	r2, r2, #1
	uxth	r2, r2
	.loc 2 2720 0
	orr	r6, r6, r0, lsl #10
	.loc 2 2728 0
	movw	r0, #65535
.LVL321:
	.loc 2 2721 0
	strh	r2, [r4, #4]	@ movhi
.L303:
	.loc 2 2723 0
	adds	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2724 0
	cmp	r1, r3
	.loc 2 2725 0
	itttt	eq
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2726 0
	moveq	r3, #0
	.loc 2 2728 0
	add	r7, r3, #8
	ldrh	r7, [r4, r7, lsl #1]
	cmp	r7, r0
	beq	.L303
	strb	r3, [r4, #6]
	.loc 2 2730 0
	ldrh	r1, [r4, #2]
	ldrh	r3, [r5, #78]
	cmp	r1, r3
	bne	.L295
	.loc 2 2730 0 is_stmt 0 discriminator 1
	cbz	r2, .L295
	.loc 2 2730 0 discriminator 2
	movw	r2, #2730
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.LVL322:
.L295:
	.loc 2 2732 0 is_stmt 1
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.LVL323:
.L301:
	.loc 2 2713 0
	adds	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2714 0
	cmp	r3, r1
	.loc 2 2713 0
	strb	r3, [r4, #6]
	.loc 2 2715 0
	itttt	eq
	ldrheq	r3, [r4, #2]
	.loc 2 2716 0
	strbeq	r2, [r4, #6]
	.loc 2 2715 0
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2718 0
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L299
.L310:
	.align	2
.L309:
	.word	.LANCHOR1+111
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
	ldr	r3, .L320
	.loc 5 30 0
	mov	ip, #20
	.loc 5 22 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
.LCFI44:
	.loc 5 27 0
	mov	r9, #12
	movs	r6, #1
	.loc 5 25 0
	movs	r2, #0
	.loc 5 29 0
	mov	r7, r3
	.loc 5 25 0
	str	r2, [r3, #3140]
.L312:
	.loc 5 26 0 discriminator 1
	ldrh	r1, [r3, #8]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L315
	.loc 5 37 0 discriminator 1
	ldr	r7, .L320
	.loc 5 35 0 discriminator 1
	mov	ip, #12
	movs	r6, #0
.L316:
	.loc 5 34 0 discriminator 1
	ldr	r2, [r3, #3160]
	cmp	r1, r2
	bcc	.L319
	.loc 5 39 0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L315:
	uxth	r2, r2
	.loc 5 27 0 discriminator 3
	ldr	r8, [r3, #3144]
	mul	lr, r9, r2
	add	r1, r8, lr
	str	r6, [r1, #8]
	.loc 5 28 0 discriminator 3
	ldrh	r1, [r3, #86]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r0, r1, #3
	ldr	r1, [r3, #3148]
	add	r1, r1, r0
	str	r1, [r8, lr]
	.loc 5 29 0 discriminator 3
	ldr	r5, [r7, #3152]
	ldr	r0, [r3, #3144]
	ldrh	r1, [r3, #88]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r1, r1, #3
	add	r8, r0, lr
	add	r1, r1, r5
	str	r1, [r8, #4]
	.loc 5 30 0 discriminator 3
	ldr	r1, [r3, #3156]
	mla	r2, ip, r2, r1
	ldr	r1, [r0, lr]
	str	r1, [r2, #8]
	.loc 5 31 0 discriminator 3
	ldr	r1, [r8, #4]
	str	r1, [r2, #12]
	mov	r2, r4
	b	.L312
.L319:
	.loc 5 35 0 discriminator 3
	mul	r0, ip, r1
	ldr	r4, [r3, #3144]
	.loc 5 36 0 discriminator 3
	ldr	r5, [r3, #3148]
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
	ldr	r2, [r3, #3144]
	ldr	r4, [r7, #3152]
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
	b	.L316
.L321:
	.align	2
.L320:
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
.LVL324:
	.loc 5 47 0
	ldr	r3, .L329
	.loc 5 42 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI45:
	.loc 5 46 0
	movs	r5, #0
	.loc 5 48 0
	mov	r10, #20
	mov	lr, #12
	.loc 5 49 0
	mov	r8, r5
	.loc 5 47 0
	ldr	ip, [r3, #3160]
	.loc 5 48 0
	ldr	r6, [r3, #3144]
.L323:
	uxth	r3, r5
	.loc 5 46 0 discriminator 1
	cmp	r1, r3
	bls	.L322
	.loc 5 48 0
	mla	r4, r10, r3, r0
	movs	r2, #0
.L324:
	uxth	r3, r2
	.loc 5 47 0 discriminator 1
	cmp	ip, r3
	bls	.L325
	.loc 5 48 0
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L324
	.loc 5 49 0
	str	r8, [r9, #8]
.L325:
	adds	r5, r5, #1
	b	.L323
.L322:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L330:
	.align	2
.L329:
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
.LVL325:
	.loc 5 62 0
	ldr	r3, .L338
	.loc 5 63 0
	mov	ip, #12
	.loc 5 57 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
.LCFI46:
	.loc 5 61 0
	movs	r4, #0
	.loc 5 64 0
	mov	lr, #1
	.loc 5 65 0
	mov	r8, #20
	.loc 5 62 0
	ldr	r5, [r3, #3160]
	.loc 5 63 0
	ldr	r6, [r3, #3144]
.L332:
	uxth	r2, r4
	.loc 5 61 0 discriminator 1
	cmp	r1, r2
	bhi	.L336
	.loc 5 71 0
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L336:
	mov	r9, #0
.L333:
	uxth	r3, r9
	.loc 5 62 0 discriminator 1
	cmp	r5, r3
	bls	.L334
	.loc 5 63 0
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L333
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
.L334:
	adds	r4, r4, #1
	b	.L332
.L339:
	.align	2
.L338:
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
.LVL326:
	.loc 5 78 0
	ldr	r2, .L345
	ldr	r3, [r2, #3164]
	ldrh	r2, [r2, #3168]
	add	r2, r3, r2, lsl #1
.L341:
	.loc 5 77 0 discriminator 1
	cmp	r3, r2
	bne	.L343
	.loc 5 81 0
	movs	r0, #0
.LVL327:
	bx	lr
.LVL328:
.L343:
	.loc 5 78 0
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L341
	.loc 5 79 0
	movs	r0, #1
.LVL329:
	.loc 5 82 0
	bx	lr
.L346:
	.align	2
.L345:
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
.LVL330:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI47:
	.loc 5 85 0
	mov	r5, r0
	.loc 5 88 0
	ubfx	r0, r0, #10, #16
.LVL331:
	.loc 5 85 0
	mov	r6, r1
	mov	r7, r2
	.loc 5 91 0
	movs	r4, #0
	.loc 5 88 0
	bl	P2V_block_in_plane
.LVL332:
	.loc 5 91 0
	ldr	r3, .L351
	ldrh	r1, [r3, #3168]
	.loc 5 92 0
	ldr	r2, [r3, #3164]
.LVL333:
.L348:
	uxth	ip, r4
.LVL334:
	.loc 5 91 0 discriminator 1
	cmp	ip, r1
	bcc	.L350
	.loc 5 96 0
	bne	.L349
	.loc 5 97 0
	strh	r0, [r2, ip, lsl #1]	@ movhi
.LVL335:
	.loc 5 98 0
	ldrh	r0, [r3, #3168]
.LVL336:
	adds	r0, r0, #1
	strh	r0, [r3, #3168]	@ movhi
	b	.L349
.LVL337:
.L350:
	adds	r4, r4, #1
	.loc 5 92 0
	add	ip, r2, r4, lsl #1
.LVL338:
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L348
.LVL339:
.L349:
	.loc 5 101 0
	ldrh	r2, [r3, #3176]
	movs	r0, #12
.LVL340:
	muls	r0, r2, r0
	ldr	r2, [r3, #3172]
	adds	r1, r2, r0
	str	r6, [r1, #4]
	.loc 5 102 0
	str	r7, [r1, #8]
	.loc 5 103 0
	str	r5, [r2, r0]
	.loc 5 107 0
	ldrh	r2, [r3, #3176]
	adds	r2, r2, #1
	strh	r2, [r3, #3176]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.LVL341:
.L352:
	.align	2
.L351:
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
.LVL342:
	push	{r4, lr}
	.save {r4, lr}
.LCFI48:
	.loc 5 347 0
	mov	r1, r0
	.loc 5 345 0
	mov	r4, r0
	.loc 5 347 0
	ldr	r0, .L356
.LVL343:
	bl	printk
.LVL344:
	.loc 5 349 0
	ldr	r3, .L356+4
	ldrh	r0, [r3, #3178]
	cmp	r4, r0
	beq	.L354
	.loc 5 349 0 is_stmt 0 discriminator 1
	ldrh	r1, [r3, #3180]
	cmp	r4, r1
	beq	.L354
	.loc 5 351 0 is_stmt 1
	movw	r2, #65535
	cmp	r0, r2
	bne	.L355
	.loc 5 352 0
	strh	r4, [r3, #3178]	@ movhi
.L354:
	.loc 5 356 0
	movs	r0, #0
	pop	{r4, pc}
.LVL345:
.L355:
	.loc 5 353 0
	cmp	r1, r2
	.loc 5 354 0
	it	eq
	strheq	r4, [r3, #3180]	@ movhi
	b	.L354
.L357:
	.align	2
.L356:
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
.LVL346:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI49:
	.loc 5 359 0
	mov	r5, r0
	.loc 5 364 0
	ldr	r4, .L362
	.loc 5 361 0
	bl	P2V_block_in_plane
.LVL347:
	.loc 5 364 0
	mov	r2, r5
	.loc 5 361 0
	mov	r6, r0
	.loc 5 364 0
	ldrh	r1, [r4, #3182]
	ldr	r0, .L362+4
	bl	printk
.LVL348:
	.loc 5 366 0
	mov	r0, r6
	bl	FtlGcRefreshBlock
.LVL349:
	.loc 5 368 0
	ldrh	r3, [r4, #3182]
	movs	r2, #0
	.loc 5 369 0
	add	r0, r4, #3184
.L359:
	.loc 5 368 0 discriminator 1
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L361
	.loc 5 372 0
	cmp	r3, #15
	.loc 5 373 0
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3182]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3184]	@ movhi
	b	.L360
.L361:
	adds	r2, r2, #1
	.loc 5 369 0
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L359
.L360:
	.loc 5 375 0
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.LVL350:
.L363:
	.align	2
.L362:
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
.LCFI50:
	.loc 5 380 0
	ldr	r4, .L370
	ldrh	r3, [r4, #3182]
	cbz	r3, .L365
	.loc 5 380 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #3178]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L365
	.loc 5 381 0 is_stmt 1
	ldrh	r2, [r4, #3218]
	cmp	r2, r3
	.loc 5 382 0
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #3218]	@ movhi
.LBB297:
.LBB298:
	.loc 5 383 0
	ldrh	r3, [r4, #3218]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #3184]
	bl	P2V_block_in_plane
.LVL351:
	bl	FtlGcRefreshBlock
.LVL352:
	.loc 5 384 0
	ldrh	r3, [r4, #3218]
	adds	r3, r3, #1
	strh	r3, [r4, #3218]	@ movhi
.L365:
.LBE298:
.LBE297:
	.loc 5 387 0
	movs	r0, #0
	pop	{r4, pc}
.L371:
	.align	2
.L370:
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
.LVL353:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI51:
	mov	r9, r1
	.loc 1 46 0
	ldr	r5, .L390
	mov	r4, r0
	.loc 1 50 0
	movs	r6, #0
.LVL354:
	.loc 1 44 0
	.pad #20
	sub	sp, sp, #20
.LCFI52:
	.loc 1 51 0
	ldr	r10, .L390+4
	.loc 1 46 0
	ldrh	r7, [r5, #12]
.LVL355:
	.loc 1 51 0
	ldr	fp, .L390+8
	.loc 1 53 0
	ldr	r8, .L390+12
	str	r5, [sp, #4]
.LVL356:
.L373:
	.loc 1 50 0 discriminator 1
	cmp	r6, r9
	bne	.L380
	.loc 1 71 0
	movs	r0, #0
.LVL357:
	add	sp, sp, #20
.LCFI53:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL358:
.L380:
.LCFI54:
	.loc 1 51 0
	ldr	r3, [r4, #8]
	cbz	r3, .L374
	.loc 1 51 0 is_stmt 0 discriminator 2
	ldr	r3, [r4, #12]
	cbnz	r3, .L375
.L374:
	.loc 1 51 0 discriminator 3
	movs	r2, #51
.LVL359:
	mov	r1, r10
	mov	r0, fp
.LVL360:
	bl	printk
.LVL361:
.L375:
	.loc 1 52 0 is_stmt 1
	add	r2, sp, #8
.LVL362:
	add	r1, sp, #12
	mov	r0, r4
.LVL363:
	bl	l2p_addr_tran
.LVL364:
	.loc 1 53 0
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #12]
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	ldr	r5, [r8, #12]
	blx	r5
.LVL365:
	.loc 1 58 0
	ldr	r3, [sp, #4]
	.loc 1 57 0
	str	r0, [r4]
	.loc 1 58 0
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L377
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
.LVL366:
	.loc 1 63 0
	adds	r0, r0, #1
	beq	.L378
	.loc 1 64 0 discriminator 1
	ldr	r3, [r4, #12]
	.loc 1 63 0 discriminator 1
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L377
	.loc 1 64 0
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L377
	.loc 1 65 0
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L377
.L378:
	.loc 1 67 0
	mov	r3, #-1
	str	r3, [r4]
.L377:
	.loc 1 50 0 discriminator 2
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L373
.L391:
	.align	2
.L390:
	.word	g_nand_phy_info
	.word	.LANCHOR1+130
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
.LCFI55:
	.loc 4 289 0
	movs	r6, #0
.LVL367:
	.loc 4 286 0
	ldr	r4, .L402
	.loc 4 301 0
	movw	r10, #61664
	.loc 4 286 0
	ldr	r3, [r4, #3240]
	add	r7, r4, #138
	.loc 4 287 0
	ldr	r8, [r4, #3244]
	.loc 4 294 0
	addw	r9, r4, #3220
	.loc 4 286 0
	str	r3, [r4, #3228]
	.loc 4 287 0
	str	r8, [r4, #3232]
.LVL368:
.L393:
	.loc 4 289 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r6, r3
	bcc	.L398
	.loc 4 314 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL369:
.L398:
	.loc 4 291 0
	ldrh	r5, [r4, #74]
.LVL370:
	.loc 4 290 0
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.LVL371:
.L395:
	.loc 4 291 0 discriminator 2
	ldrh	r3, [r4, #74]
	subs	r5, r5, #1
.LVL372:
	uxth	r5, r5
.LVL373:
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L396
	.loc 4 292 0
	mla	r3, r6, r3, r5
	.loc 4 294 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	.loc 4 292 0
	lsls	r3, r3, #10
.LVL374:
	.loc 4 293 0
	str	r3, [r4, #3224]
	.loc 4 294 0
	bl	FlashReadPages
.LVL375:
	.loc 4 295 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	beq	.L395
	.loc 4 301 0
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L395
	.loc 4 303 0
	strh	r5, [r7]	@ movhi
.L396:
	.loc 4 289 0 discriminator 2
	adds	r6, r6, #1
	b	.L393
.L403:
	.align	2
.L402:
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
.LVL376:
	ldr	r3, .L412
	.loc 2 51 0
	cmp	r1, #1
	.loc 2 47 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI56:
	.loc 2 57 0
	lsl	r7, r0, #10
	.loc 2 47 0
	.pad #88
	sub	sp, sp, #88
.LCFI57:
	.loc 2 58 0
	mov	r2, r1
	.loc 2 47 0
	mov	r6, r1
	.loc 2 51 0
	it	eq
	ldrheq	r4, [r3, #80]
	.loc 2 58 0
	mov	r1, #1
.LVL377:
	.loc 2 51 0
	it	ne
	ldrhne	r4, [r3, #78]
	.loc 2 58 0
	add	r0, sp, #4
.LVL378:
	.loc 2 53 0
	ldr	r3, .L412+4
	.loc 2 56 0
	subs	r4, r4, #1
	sxth	r5, r4
.LVL379:
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
.LVL380:
	.loc 2 59 0
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L407
	mov	r8, #0
.LVL381:
.L408:
	.loc 2 60 0
	cmp	r8, r5
	ble	.L411
.L407:
	.loc 2 73 0
	mov	r0, r5
	add	sp, sp, #88
.LCFI58:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL382:
.L411:
.LCFI59:
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
.LVL383:
	.loc 2 65 0
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L409
	.loc 2 65 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L409
	.loc 2 66 0 is_stmt 1
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L408
.L409:
	.loc 2 68 0
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L408
.L413:
	.align	2
.L412:
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
.LVL384:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI60:
	.pad #44
	sub	sp, sp, #44
.LCFI61:
	.loc 1 74 0
	str	r3, [sp]
	mov	fp, r1
	mov	r9, r2
	mov	r4, r0
	.loc 1 76 0
	ldr	r3, .L444
.LVL385:
	.loc 1 80 0
	mov	r5, r0
.LVL386:
	.loc 1 83 0
	ldr	r8, .L444+28
	.loc 1 80 0
	movs	r6, #0
	.loc 1 76 0
	ldrh	r10, [r3, #12]
.LVL387:
	str	r3, [sp, #4]
.LVL388:
.L415:
	.loc 1 80 0 discriminator 1
	cmp	r6, fp
	bne	.L423
	.loc 1 101 0
	ldr	r3, [sp]
.LVL389:
	cmp	r3, #0
	bne	.L430
.L443:
	.loc 1 131 0
	movs	r0, #0
	add	sp, sp, #44
.LCFI62:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL390:
.L423:
.LCFI63:
	.loc 1 81 0
	ldr	r3, [r5, #8]
	cbz	r3, .L416
	.loc 1 81 0 is_stmt 0 discriminator 2
	ldr	r3, [r5, #12]
	cbnz	r3, .L417
.L416:
	.loc 1 81 0 discriminator 3
	movs	r2, #81
	ldr	r1, .L444+4
	ldr	r0, .L444+8
	bl	printk
.LVL391:
.L417:
	.loc 1 82 0 is_stmt 1
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r5
	bl	l2p_addr_tran
.LVL392:
	.loc 1 83 0
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	ldr	r7, [r8, #8]
	blx	r7
.LVL393:
	.loc 1 88 0
	cbnz	r0, .L418
	.loc 1 87 0
	str	r0, [r5]
.L419:
	.loc 1 91 0
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L421
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
.LVL394:
	.loc 1 96 0
	cbz	r0, .L421
	.loc 1 97 0
	mov	r3, #-1
	str	r3, [r5]
.L421:
	.loc 1 80 0 discriminator 2
	adds	r6, r6, #1
	adds	r5, r5, #20
	b	.L415
.L418:
	.loc 1 89 0
	mov	r3, #-1
	str	r3, [r5]
	b	.L419
.LVL395:
.L428:
.LBB299:
	.loc 1 106 0
	movs	r3, #0
.LVL396:
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
.LVL397:
	.loc 1 113 0
	ldr	fp, [sp, #20]
.LVL398:
	cmp	fp, #-1
	bne	.L425
	.loc 1 114 0
	ldr	r1, [r4, #4]
	ldr	r0, .L444+12
	bl	printk
.LVL399:
	.loc 1 115 0
	str	fp, [r4]
.L425:
	.loc 1 117 0
	ldr	r3, [r4, #12]
	cbz	r3, .L426
	.loc 1 118 0
	ldr	r2, [r3]
	ldr	r3, [r8, #3248]
	cmp	r2, r3
	beq	.L426
	.loc 1 119 0
	ldr	r1, [r4, #4]
	ldr	r0, .L444+16
	bl	printk
.LVL400:
	.loc 1 120 0
	mov	r3, #-1
	str	r3, [r4]
.L426:
	.loc 1 123 0
	ldr	r3, [r4, #8]
	cbz	r3, .L427
	.loc 1 124 0
	ldr	r2, [r3]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L427
	.loc 1 125 0
	ldr	r1, [r4, #4]
	ldr	r0, .L444+20
	bl	printk
.LVL401:
	.loc 1 126 0
	mov	r3, #-1
	str	r3, [r4]
.L427:
.LBE299:
	.loc 1 103 0 discriminator 2
	adds	r5, r5, #1
	adds	r4, r4, #20
.LVL402:
.L424:
	.loc 1 103 0 is_stmt 0 discriminator 1
	cmp	r6, r5
	bne	.L428
	b	.L443
.LVL403:
.L430:
.LBB300:
	.loc 1 107 0 is_stmt 1
	ldr	r8, .L444+32
.LBE300:
	movs	r5, #0
.LBB301:
	.loc 1 106 0
	ldr	r7, .L444+24
	.loc 1 110 0
	add	r10, r8, #3248
.LVL404:
	b	.L424
.L445:
	.align	2
.L444:
	.word	g_nand_phy_info
	.word	.LANCHOR1+145
	.word	.LC1
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	check_buf
	.word	g_nand_ops
	.word	.LANCHOR0
.LBE301:
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
.LVL405:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
.LCFI64:
.LVL406:
	.loc 1 134 0
	mov	r8, r2
	.loc 1 137 0
	ldr	r5, .L457
.LVL407:
	mov	r4, r0
	.loc 1 140 0
	movs	r6, #0
.LVL408:
	.loc 1 142 0
	ldr	r7, .L457+4
	.loc 1 137 0
	ldrh	r9, [r5, #12]
.LVL409:
.L447:
	.loc 1 140 0 discriminator 1
	cmp	r6, r8
	bne	.L453
	.loc 1 153 0
	movs	r0, #0
.LVL410:
	add	sp, sp, #12
.LCFI65:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.LVL411:
.L453:
.LCFI66:
	.loc 1 141 0
	add	r1, sp, #4
	mov	r2, sp
	mov	r0, r4
.LVL412:
	bl	l2p_addr_tran
.LVL413:
	.loc 1 142 0
	ldr	r3, [r7, #4]
	ldr	r1, [sp, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	blx	r3
.LVL414:
	.loc 1 144 0
	cbnz	r0, .L448
	.loc 1 143 0
	str	r0, [r4]
.L449:
	.loc 1 146 0
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L451
	.loc 1 147 0
	ldr	r1, [sp, #4]
	ldr	r3, [r7, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r1, r1, r9
	blx	r3
.LVL415:
	.loc 1 148 0
	cbz	r0, .L451
	.loc 1 149 0
	mov	r3, #-1
	str	r3, [r4]
.L451:
	.loc 1 140 0 discriminator 2
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L447
.L448:
	.loc 1 145 0
	mov	r3, #-1
	str	r3, [r4]
	b	.L449
.L458:
	.align	2
.L457:
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
.LVL416:
	.loc 2 99 0
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	.loc 2 98 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI67:
	.loc 2 98 0
	mov	r5, r0
	.loc 2 99 0
	cmp	r3, r2
	bhi	.L459
.LBB310:
.LBB311:
	.loc 2 94 0
	ldr	r4, .L468
.LBE311:
.LBE310:
	.loc 2 101 0
	ldrh	r3, [r4, #194]
	cmp	r3, #1024
	beq	.L459
.LBB312:
.LBB313:
	.loc 2 103 0
	cbz	r1, .L461
.LBB314:
	.loc 2 104 0
	bl	P2V_block_in_plane
.LVL417:
	mov	r6, r0
	.loc 2 105 0
	ldr	r0, [r4, #3760]
	lsls	r3, r5, #10
	.loc 2 106 0
	movs	r2, #1
	mov	r1, r2
	.loc 2 105 0
	str	r3, [r0, #4]
	.loc 2 106 0
	bl	FlashEraseBlocks
.LVL418:
	.loc 2 107 0
	ldr	r2, [r4, #2304]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	.loc 2 108 0
	ldr	r3, [r4, #2536]
	adds	r3, r3, #1
	str	r3, [r4, #2536]
.LVL419:
.L461:
.LBE314:
	.loc 2 110 0
	ldrh	r3, [r4, #194]
	adds	r3, r3, #1
	strh	r3, [r4, #194]	@ movhi
	.loc 2 111 0
	ldrh	r3, [r4, #192]
	add	r2, r4, r3, lsl #1
	.loc 2 112 0
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 111 0
	strh	r5, [r2, #196]	@ movhi
	.loc 2 112 0
	strh	r3, [r4, #192]	@ movhi
.L459:
	pop	{r4, r5, r6, pc}
.LVL420:
.L469:
	.align	2
.L468:
	.word	.LANCHOR0
.LBE313:
.LBE312:
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
.LVL421:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI68:
	.loc 3 539 0
	mov	r10, #0
	.loc 3 538 0
	ldr	r4, .L515
	.loc 3 528 0
	mov	r7, r1
	.loc 3 536 0
	mov	r6, r10
	.loc 3 537 0
	mov	r5, r10
.LVL422:
	.loc 3 540 0
	mov	r9, #20
	.loc 3 528 0
	.pad #20
	sub	sp, sp, #20
.LCFI69:
	.loc 3 551 0
	mov	fp, r4
	.loc 3 528 0
	str	r0, [sp]
	.loc 3 538 0
	str	r0, [r4, #3764]
.L471:
	.loc 3 539 0 discriminator 1
	ldrh	r1, [r4, #8]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L476
	.loc 3 559 0
	cmp	r6, #0
	beq	.L470
	.loc 3 563 0
	mov	r8, #0
	.loc 3 564 0
	mov	r9, #20
	.loc 3 562 0
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3760]
.LVL423:
	bl	FlashEraseBlocks
.LVL424:
.L479:
	.loc 3 563 0 discriminator 1
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L481
	.loc 3 571 0
	cmp	r7, #0
	beq	.L498
	.loc 3 573 0
	ldrh	r3, [r4, #80]
.LVL425:
	.loc 3 572 0
	mov	r10, #1
.LVL426:
	.loc 3 573 0
	str	r3, [sp, #4]
	.loc 3 574 0
	lsrs	r3, r3, #2
.LVL427:
	str	r3, [sp, #8]
.LVL428:
.L482:
	mov	r8, #0
.LVL429:
	.loc 3 579 0
	mov	fp, #20
.L492:
	.loc 3 578 0
	mov	r9, #0
	.loc 3 577 0
	mov	r6, r9
.L483:
	.loc 3 578 0 discriminator 1
	ldrh	r1, [r4, #8]
	uxth	r3, r9
.LVL430:
	cmp	r1, r3
	bhi	.L487
	.loc 3 593 0
	cbz	r6, .L470
	.loc 3 597 0
	mov	r9, #0
	.loc 3 596 0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3760]
	bl	FlashProgPages
.LVL431:
.L489:
	.loc 3 597 0 discriminator 1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L491
	.loc 3 604 0
	ldr	r3, [sp, #8]
.LVL432:
	add	r8, r8, r3
.LVL433:
	.loc 3 605 0
	ldr	r3, [sp, #4]
.LVL434:
	.loc 3 604 0
	uxth	r8, r8
.LVL435:
	.loc 3 605 0
	cmp	r3, r8
	bhi	.L492
	mov	r8, #0
.LVL436:
	.loc 3 611 0 discriminator 1
	mov	r9, #20
.L493:
	.loc 3 610 0 discriminator 1
	uxth	r3, r8
.LVL437:
	cmp	r6, r3
	bhi	.L495
	.loc 3 615 0
	ldr	r3, [sp]
.LVL438:
	cmp	r3, #63
	bls	.L496
	.loc 3 615 0 is_stmt 0 discriminator 1
	cbz	r7, .L470
.L496:
	.loc 3 616 0 is_stmt 1
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3760]
	bl	FlashEraseBlocks
.LVL439:
.L470:
	.loc 3 619 0
	mov	r0, r5
.LVL440:
	add	sp, sp, #20
.LCFI70:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL441:
.L476:
.LCFI71:
	.loc 3 540 0
	uxth	r3, r10
	ldr	r0, [r4, #3760]
.LVL442:
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
.LVL443:
	mov	r8, r0
	.loc 3 543 0
	cbz	r7, .L472
	.loc 3 544 0
	bl	IsBlkInVendorPart
.LVL444:
	cbnz	r0, .L473
.L472:
	.loc 3 548 0
	mov	r0, r8
	bl	FtlBbmIsBadBlock
.LVL445:
	cbnz	r0, .L474
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
.L473:
	add	r10, r10, #1
	b	.L471
.L474:
	.loc 3 554 0
	adds	r5, r5, #1
.LVL446:
	uxth	r5, r5
.LVL447:
	b	.L473
.L481:
	.loc 3 564 0
	mul	r3, r9, r8
	ldr	r2, [r4, #3760]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L480
	.loc 3 566 0
	ldr	r0, [r1, #4]
	.loc 3 565 0
	adds	r5, r5, #1
.LVL448:
	uxth	r5, r5
.LVL449:
	.loc 3 566 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL450:
.L480:
	add	r8, r8, #1
	b	.L479
.L498:
	.loc 3 533 0
	movs	r3, #6
.LVL451:
	.loc 3 534 0
	mov	r10, r7
.LVL452:
	.loc 3 533 0
	str	r3, [sp, #8]
	.loc 3 531 0
	movs	r3, #1
.LVL453:
	str	r3, [sp, #4]
	b	.L482
.LVL454:
.L487:
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
.LVL455:
	str	r0, [sp, #12]
	.loc 3 581 0
	cbz	r7, .L484
	.loc 3 582 0
	bl	IsBlkInVendorPart
.LVL456:
	cbnz	r0, .L485
.L484:
	.loc 3 585 0
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
.LVL457:
	cbnz	r0, .L485
	.loc 3 586 0
	ldr	r1, [r4, #3760]
	ldr	r3, [sp, #12]
	.loc 3 588 0
	ldr	r2, .L515
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
.L485:
	add	r9, r9, #1
	b	.L483
.L491:
	.loc 3 598 0
	mul	r3, fp, r9
	ldr	r2, [r4, #3760]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L490
	.loc 3 600 0
	ldr	r0, [r1, #4]
	.loc 3 599 0
	adds	r5, r5, #1
.LVL458:
	uxth	r5, r5
.LVL459:
	.loc 3 600 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL460:
.L490:
	add	r9, r9, #1
	b	.L489
.LVL461:
.L495:
	.loc 3 611 0
	cbz	r7, .L494
	.loc 3 611 0 is_stmt 0 discriminator 1
	mul	r3, r9, r8
	ldr	r2, [r4, #3760]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L494
	.loc 3 612 0 is_stmt 1
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.LVL462:
.L494:
	add	r8, r8, #1
	b	.L493
.L516:
	.align	2
.L515:
	.word	.LANCHOR0
.LFE159:
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	.loc 2 181 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI72:
.LBB315:
.LBB316:
	.loc 2 89 0
	ldr	r4, .L524
.LBE316:
.LBE315:
	.loc 2 203 0
	ldr	r6, .L524+4
.L518:
.LBB318:
.LBB317:
	.loc 2 89 0
	ldrh	r1, [r4, #194]
.LBE317:
.LBE318:
	.loc 2 186 0
	cbz	r1, .L519
	.loc 2 187 0
	ldrh	r3, [r4, #190]
	.loc 2 188 0
	subs	r1, r1, #1
	strh	r1, [r4, #194]	@ movhi
	.loc 2 187 0
	add	r2, r4, r3, lsl #1
	.loc 2 189 0
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 187 0
	ldrh	r5, [r2, #196]
	.loc 2 189 0
	strh	r3, [r4, #190]	@ movhi
.LBB319:
	.loc 2 191 0
	mov	r0, r5
	bl	P2V_block_in_plane
.LVL463:
	mov	r7, r0
	.loc 2 192 0
	ldr	r0, [r4, #3760]
	lsls	r3, r5, #10
	.loc 2 193 0
	movs	r2, #1
	mov	r1, r2
	.loc 2 192 0
	str	r3, [r0, #4]
	.loc 2 193 0
	bl	FlashEraseBlocks
.LVL464:
	.loc 2 194 0
	ldr	r2, [r4, #2304]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
.LBE319:
	.loc 2 202 0
	movw	r2, #65533
.LBB320:
	.loc 2 195 0
	ldr	r3, [r4, #2536]
	adds	r3, r3, #1
	str	r3, [r4, #2536]
.LBE320:
	.loc 2 202 0
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L523
	.loc 2 203 0
	ldrh	r2, [r4, #194]
	mov	r1, r5
	mov	r0, r6
	bl	printk
.LVL465:
	.loc 2 205 0
	b	.L518
.L519:
	.loc 2 199 0
	ldr	r0, .L524+8
	bl	printk
.LVL466:
.L522:
	b	.L522
.L523:
	.loc 2 208 0
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L525:
	.align	2
.L524:
	.word	.LANCHOR0
	.word	.LC79
	.word	.LC78
.LFE193:
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
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
	.loc 2 562 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL467:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI73:
	.loc 2 562 0
	mov	r4, r0
	.loc 2 566 0
	ldrh	r1, [r0, #10]
	movs	r3, #0
	ldr	r2, [r0, #12]
.LVL468:
.L527:
	uxth	r5, r3
	.loc 2 566 0 is_stmt 0 discriminator 1
	cmp	r5, r1
	bcs	.L530
	.loc 2 567 0 is_stmt 1
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L527
	.loc 2 568 0
	bl	FtlFreeSysBlkQueueOut
.LVL469:
	.loc 2 569 0
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	.loc 2 568 0
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	.loc 2 569 0
	cmp	r3, r2
	bls	.L528
	.loc 2 570 0
	ldr	r3, .L534
	ldr	r0, .L534+4
	ldrh	r2, [r3, #194]
	bl	printk
.LVL470:
.L529:
	b	.L529
.L528:
	.loc 2 576 0
	ldr	r3, [r4, #28]
	.loc 2 574 0
	strh	r6, [r4, #2]	@ movhi
	.loc 2 575 0
	strh	r5, [r4]	@ movhi
	.loc 2 576 0
	adds	r3, r3, #1
	str	r3, [r4, #28]
	.loc 2 577 0
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.LVL471:
.L530:
	.loc 2 581 0
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L532
.LBB323:
.LBB324:
	movw	r2, #581
	ldr	r1, .L534+8
	ldr	r0, .L534+12
	bl	printk
.LVL472:
.L532:
.LBE324:
.LBE323:
	.loc 2 583 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL473:
.L535:
	.align	2
.L534:
	.word	.LANCHOR0
	.word	.LC80
	.word	.LANCHOR1+160
	.word	.LC1
.LFE205:
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
.LVL474:
	.loc 1 161 0
	cmp	r0, #11
	.loc 1 156 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
.LCFI74:
	mov	r4, r0
	.pad #92
	sub	sp, sp, #92
.LCFI75:
	.loc 1 161 0
	bls	.L538
.LBB337:
.LBB338:
	.loc 1 165 0
	ldr	r5, .L539
	.loc 1 166 0
	add	r0, sp, #24
.LBB339:
.LBB340:
.LBB341:
	.loc 1 202 0
	movs	r2, #32
	movs	r1, #165
.LBE341:
.LBE340:
.LBE339:
	.loc 1 169 0
	lsls	r4, r4, #10
.LVL475:
	.loc 1 166 0
	str	r0, [sp, #16]
	.loc 1 165 0
	str	r5, [sp, #12]
.LBB344:
.LBB343:
.LBB342:
	.loc 1 202 0
	bl	memset
.LVL476:
.LBE342:
.LBE343:
.LBE344:
	.loc 1 170 0
	movs	r2, #1
.LBB345:
.LBB346:
.LBB347:
	.loc 1 202 0
	mov	r3, #1515870810
.LBE347:
.LBE346:
.LBE345:
	.loc 1 170 0
	mov	r1, r2
	add	r0, sp, #4
.LBB350:
.LBB349:
.LBB348:
	.loc 1 202 0
	str	r3, [r5]
	str	r3, [r5, #4]
.LBE348:
.LBE349:
.LBE350:
	.loc 1 169 0
	str	r4, [sp, #8]
	.loc 1 170 0
	bl	FlashEraseBlocks
.LVL477:
	.loc 1 171 0
	movs	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL478:
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
.LVL479:
.L536:
.LBE338:
.LBE337:
	.loc 1 180 0
	mov	r0, r4
.LVL480:
	add	sp, sp, #92
.LCFI76:
	@ sp needed
	pop	{r4, r5, pc}
.LVL481:
.L538:
.LCFI77:
	.loc 1 162 0
	movs	r4, #0
.LVL482:
	b	.L536
.L540:
	.align	2
.L539:
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
.LVL483:
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI78:
.LBB361:
.LBB362:
.LBB363:
	.loc 1 202 0
	mov	r2, #256
.LBE363:
.LBE362:
.LBE361:
	.loc 1 183 0
	mov	r5, r1
.LBB368:
.LBB366:
.LBB364:
	.loc 1 202 0
	movs	r1, #255
.LVL484:
.LBE364:
.LBE366:
.LBE368:
	.loc 1 183 0
	mov	r4, r0
.LBB369:
.LBB367:
.LBB365:
	.loc 1 202 0
	bl	memset
.LVL485:
.LBE365:
.LBE367:
.LBE369:
	.loc 1 187 0
	ldr	r3, .L548
	mov	r1, r5
	mov	r0, r4
	ldr	r3, [r3]
	blx	r3
.LVL486:
	uxth	r0, r0
.LVL487:
	.loc 1 188 0
	cmp	r0, #50
	bls	.L542
.LBB370:
.LBB371:
.LBB372:
	.loc 1 202 0
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
.LVL488:
	bl	memset
.LVL489:
.LBE372:
.LBE371:
.LBE370:
	.loc 1 190 0
	movs	r0, #0
.LVL490:
.L542:
	.loc 1 192 0
	ldr	r3, .L548+4
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L547
	add	r1, r4, r0, lsl #1
.LVL491:
	mov	r3, r4
.L544:
	.loc 1 193 0 discriminator 1
	cmp	r3, r1
	bne	.L545
.L547:
.LVL492:
	.loc 1 198 0
	pop	{r3, r4, r5, pc}
.LVL493:
.L545:
	.loc 1 194 0 discriminator 3
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L544
.L549:
	.align	2
.L548:
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
.LVL494:
	push	{r4, lr}
	.save {r4, lr}
.LCFI79:
	.loc 1 201 0
	mov	r4, r0
.LBB377:
	.loc 1 202 0
	cbz	r2, .L551
.LBE377:
.LBB378:
.LBB379:
	bl	memset
.LVL495:
.L551:
.LBE379:
.LBE378:
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
.LCFI80:
	.loc 3 297 0
	movw	r3, #65535
	.loc 3 282 0
	ldr	r4, .L656
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
	strh	r3, [r4, #3178]	@ movhi
	.loc 3 302 0
	strh	r3, [r4, #3180]	@ movhi
	.loc 3 303 0
	movs	r3, #32
	strh	r3, [r4, #3124]	@ movhi
	.loc 3 310 0
	lsls	r0, r0, #1
	.loc 3 304 0
	movs	r3, #128
	.loc 3 282 0
	str	r5, [r4, #2516]
	.loc 3 304 0
	strh	r3, [r4, #3126]	@ movhi
	.loc 3 283 0
	str	r5, [r4, #2520]
	.loc 3 284 0
	str	r5, [r4, #2504]
	.loc 3 285 0
	str	r5, [r4, #2492]
	.loc 3 286 0
	str	r5, [r4, #2488]
	.loc 3 287 0
	str	r5, [r4, #2496]
	.loc 3 288 0
	str	r5, [r4, #2500]
	.loc 3 289 0
	str	r5, [r4, #2484]
	.loc 3 290 0
	str	r5, [r4, #2524]
	.loc 3 291 0
	str	r5, [r4, #2528]
	.loc 3 292 0
	str	r5, [r4, #2536]
	.loc 3 293 0
	str	r5, [r4, #2540]
	.loc 3 294 0
	str	r5, [r4, #2544]
	.loc 3 295 0
	str	r5, [r4, #3780]
	.loc 3 296 0
	str	r5, [r4, #3136]
	.loc 3 298 0
	str	r5, [r4, #3788]
	.loc 3 299 0
	str	r5, [r4, #3128]
	.loc 3 300 0
	str	r5, [r4, #3764]
	.loc 3 305 0
	strh	r5, [r4, #3132]	@ movhi
	.loc 3 306 0
	strh	r5, [r4, #3182]	@ movhi
	.loc 3 307 0
	strh	r5, [r4, #3218]	@ movhi
	.loc 3 310 0
	bl	ftl_malloc
.LVL496:
	str	r0, [r4, #3164]
	.loc 3 311 0
	ldrh	r0, [r4, #82]
	muls	r0, r6, r0
	bl	ftl_malloc
.LVL497:
	.loc 3 312 0
	ldrh	r3, [r4, #8]
	.loc 3 311 0
	str	r0, [r4, #3172]
	.loc 3 312 0
	muls	r7, r3, r7
	.loc 3 313 0
	lsl	r8, r7, #2
	mov	r0, r8
	bl	ftl_malloc
.LVL498:
	str	r0, [r4, #3792]
	.loc 3 314 0
	mov	r0, r7
	bl	ftl_malloc
.LVL499:
	str	r0, [r4, #3796]
	.loc 3 315 0
	mov	r0, r8
	bl	ftl_malloc
.LVL500:
	str	r0, [r4, #3800]
	.loc 3 316 0
	mov	r0, r7
	bl	ftl_malloc
.LVL501:
	str	r0, [r4, #3760]
	.loc 3 317 0
	mov	r0, r7
	bl	ftl_malloc
.LVL502:
	.loc 3 320 0
	ldrh	r3, [r4, #8]
	.loc 3 319 0
	ldrh	r7, [r4, #86]
	.loc 3 317 0
	str	r0, [r4, #3156]
	.loc 3 320 0
	lsls	r3, r3, #1
	.loc 3 321 0
	mov	r0, r7
	.loc 3 320 0
	adds	r3, r3, #1
	str	r3, [r4, #3160]
	.loc 3 321 0
	bl	ftl_malloc
.LVL503:
	str	r0, [r4, #3240]
	.loc 3 322 0
	mov	r0, r7
	bl	ftl_malloc
.LVL504:
	str	r0, [r4, #3804]
	.loc 3 323 0
	mov	r0, r7
	bl	ftl_malloc
.LVL505:
	str	r0, [r4, #3808]
	.loc 3 324 0
	ldr	r0, [r4, #3160]
	muls	r0, r7, r0
	bl	ftl_malloc
.LVL506:
	str	r0, [r4, #3148]
	.loc 3 325 0
	mov	r0, r7
	bl	ftl_malloc
.LVL507:
	str	r0, [r4, #3776]
	.loc 3 326 0
	mov	r0, r7
	bl	ftl_malloc
.LVL508:
	str	r0, [r4, #3768]
	.loc 3 327 0
	ldr	r0, [r4, #3160]
	muls	r0, r6, r0
	bl	ftl_malloc
.LVL509:
	.loc 3 329 0
	ldrh	r3, [r4, #88]
	ldrh	r7, [r4, #8]
	.loc 3 327 0
	str	r0, [r4, #3144]
	.loc 3 329 0
	muls	r7, r3, r7
	.loc 3 330 0
	mov	r0, r7
	bl	ftl_malloc
.LVL510:
	str	r0, [r4, #3244]
	.loc 3 331 0
	lsls	r0, r7, #2
	bl	ftl_malloc
.LVL511:
	.loc 3 332 0
	ldrh	r3, [r4, #88]
	.loc 3 331 0
	str	r0, [r4, #3772]
	.loc 3 332 0
	ldr	r0, [r4, #3160]
	muls	r0, r3, r0
	bl	ftl_malloc
.LVL512:
	str	r0, [r4, #3152]
	.loc 3 335 0
	ldrh	r0, [r4, #18]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3812]	@ movhi
	.loc 3 336 0
	bl	ftl_malloc
.LVL513:
	.loc 3 337 0
	ldrh	r3, [r4, #3812]
	.loc 3 336 0
	str	r0, [r4, #3816]
	.loc 3 338 0
	ldr	r0, .L656+4
	.loc 3 337 0
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	.loc 3 338 0
	and	r0, r0, r3, lsl #9
	.loc 3 337 0
	strh	r3, [r4, #3812]	@ movhi
	.loc 3 338 0
	bl	ftl_malloc
.LVL514:
	.loc 3 345 0
	ldrh	r7, [r4, #18]
	.loc 3 338 0
	str	r0, [r4, #3820]
	.loc 3 339 0
	adds	r0, r0, #32
	str	r0, [r4, #2304]
	.loc 3 345 0
	lsls	r7, r7, #1
	.loc 3 347 0
	mov	r0, r7
	bl	ftl_malloc
.LVL515:
	str	r0, [r4, #3824]
	.loc 3 349 0
	mov	r0, r7
	bl	ftl_malloc
.LVL516:
	.loc 3 350 0
	ldr	r7, [r4, #104]
	.loc 3 349 0
	str	r0, [r4, #2300]
	.loc 3 350 0
	lsls	r7, r7, #1
	.loc 3 351 0
	mov	r0, r7
	bl	ftl_malloc
.LVL517:
	str	r0, [r4, #3828]
	.loc 3 352 0
	mov	r0, r7
	bl	ftl_malloc
.LVL518:
	str	r0, [r4, #3832]
	.loc 3 353 0
	ldrh	r0, [r4, #18]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
.LVL519:
	str	r0, [r4]
	.loc 3 354 0
	ldrh	r0, [r4, #96]
	lsls	r0, r0, #1
	bl	ftl_malloc
.LVL520:
	str	r0, [r4, #124]
	.loc 3 355 0
	ldrh	r0, [r4, #96]
	lsls	r0, r0, #1
	bl	ftl_malloc
.LVL521:
	str	r0, [r4, #3836]
	.loc 3 356 0
	ldrh	r0, [r4, #96]
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL522:
	str	r0, [r4, #3840]
	.loc 3 357 0
	ldrh	r0, [r4, #98]
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL523:
	.loc 3 358 0
	ldrh	r2, [r4, #98]
	mov	r1, r5
	.loc 3 357 0
	str	r0, [r4, #3844]
	add	r5, r4, #156
	.loc 3 358 0
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL524:
	.loc 3 360 0
	ldrh	r0, [r4, #112]
	.loc 3 361 0
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL525:
	str	r0, [r4, #3848]
	.loc 3 362 0
	ldr	r0, [r4, #104]
	.loc 3 363 0
	lsls	r0, r0, #2
	bl	ftl_malloc
.LVL526:
	str	r0, [r4, #3852]
	.loc 3 364 0
	ldrh	r0, [r4, #114]
	.loc 3 365 0
	muls	r0, r6, r0
	bl	ftl_malloc
.LVL527:
	.loc 3 366 0
	ldrh	r3, [r4, #114]
	.loc 3 365 0
	str	r0, [r4, #2468]
	.loc 3 366 0
	ldrh	r0, [r4, #86]
	.loc 3 367 0
	muls	r0, r3, r0
	bl	ftl_malloc
.LVL528:
	.loc 3 368 0
	ldrh	r3, [r4, #18]
	.loc 3 367 0
	str	r0, [r4, #3856]
	.loc 3 369 0
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
.LVL529:
	str	r0, [r4, #2292]
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
.LVL530:
	.loc 3 374 0
	ldrh	r2, [r4, #3860]
	.loc 3 373 0
	movs	r3, #1
.LVL531:
	ldrh	r6, [r4, #30]
	.loc 3 372 0
	str	r0, [r4, #156]
	lsls	r2, r2, #2
	.loc 3 373 0
	mov	r1, r2
.L556:
	.loc 3 373 0 is_stmt 0 discriminator 1
	cmp	r3, r6
	bcc	.L557
	ldr	r2, .L656+8
	.loc 3 379 0 is_stmt 1
	movs	r1, #0
	add	r3, r2, r3, lsl #2
.LVL532:
	adds	r2, r2, #56
	adds	r3, r3, #24
.L558:
	.loc 3 377 0 discriminator 1
	cmp	r2, r3
	bne	.L559
	.loc 3 383 0
	ldr	r3, [r4, #3828]
	cbnz	r3, .L560
.L562:
	.loc 3 385 0
	ldr	r1, .L656+12
	ldr	r0, .L656+16
	bl	printk
.LVL533:
	.loc 3 386 0
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.LVL534:
.L557:
	.loc 3 374 0 discriminator 3
	ldr	r0, [r4, #156]
	.loc 3 373 0 discriminator 3
	adds	r3, r3, #1
	.loc 3 374 0 discriminator 3
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L556
.LVL535:
.L559:
	.loc 3 379 0 discriminator 2
	str	r1, [r3, #4]!
	b	.L558
.L657:
	.align	2
.L656:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+128
	.word	.LANCHOR1+186
	.word	.LC81
.L560:
	.loc 3 388 0
	ldr	r3, [r4, #3832]
	cmp	r3, #0
	beq	.L562
	.loc 3 393 0
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L562
	.loc 3 398 0
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L562
	.loc 3 404 0
	ldr	r3, [r4, #2468]
	cmp	r3, #0
	beq	.L562
	.loc 3 409 0
	ldr	r3, [r4, #3856]
	cmp	r3, #0
	beq	.L562
	.loc 3 414 0
	ldr	r3, [r4, #2292]
	cmp	r3, #0
	beq	.L562
	.loc 3 419 0
	ldr	r3, [r4, #156]
	cmp	r3, #0
	beq	.L562
	.loc 3 425 0
	ldr	r3, [r4, #2300]
	cmp	r3, #0
	beq	.L562
	.loc 3 430 0
	ldr	r3, [r4, #3164]
	cmp	r3, #0
	beq	.L562
	.loc 3 430 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L562
	.loc 3 435 0 is_stmt 1
	ldr	r3, [r4, #3792]
	cmp	r3, #0
	beq	.L562
	.loc 3 435 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3800]
	cmp	r3, #0
	beq	.L562
	.loc 3 435 0 discriminator 2
	ldr	r3, [r4, #3760]
	cmp	r3, #0
	beq	.L562
	.loc 3 435 0 discriminator 3
	ldr	r3, [r4, #3156]
	cmp	r3, #0
	beq	.L562
	.loc 3 435 0 discriminator 4
	ldr	r3, [r4, #3796]
	cmp	r3, #0
	beq	.L562
	.loc 3 440 0 is_stmt 1
	ldr	r3, [r4, #3240]
	cmp	r3, #0
	beq	.L562
	.loc 3 440 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L562
	.loc 3 440 0 discriminator 2
	ldr	r3, [r4, #3808]
	cmp	r3, #0
	beq	.L562
	.loc 3 440 0 discriminator 3
	ldr	r3, [r4, #3148]
	cmp	r3, #0
	beq	.L562
	.loc 3 441 0 is_stmt 1
	ldr	r3, [r4, #3776]
	cmp	r3, #0
	beq	.L562
	.loc 3 441 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3768]
	cmp	r3, #0
	beq	.L562
	.loc 3 441 0 discriminator 2
	ldr	r3, [r4, #3144]
	cmp	r3, #0
	beq	.L562
	.loc 3 446 0 is_stmt 1
	ldr	r3, [r4, #3244]
	cmp	r3, #0
	beq	.L562
	.loc 3 446 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3772]
	cmp	r3, #0
	beq	.L562
	.loc 3 446 0 discriminator 2
	ldr	r3, [r4, #3152]
	cmp	r3, #0
	beq	.L562
	.loc 3 452 0 is_stmt 1
	ldr	r3, [r4, #2304]
	cmp	r3, #0
	beq	.L562
	.loc 3 452 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L562
	.loc 3 458 0 is_stmt 1
	ldr	r3, [r4, #124]
	cmp	r3, #0
	beq	.L562
	.loc 3 464 0
	ldr	r3, .L658
	ldr	r2, [r3, #3836]
	cmp	r2, #0
	beq	.L562
	.loc 3 470 0
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L562
	.loc 3 476 0
	ldr	r3, [r3, #3844]
	cmp	r3, #0
	beq	.L562
	.loc 3 483 0
	movs	r0, #0
	.loc 3 484 0
	pop	{r4, r5, r6, r7, r8, pc}
.L659:
	.align	2
.L658:
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
.LVL536:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI81:
	.loc 4 66 0
	mov	r5, r0
	.loc 4 69 0
	ldr	r6, .L666
	.loc 4 66 0
	mov	r7, r1
	subs	r4, r5, #2
	.loc 4 72 0
	movw	r8, #65535
	.loc 4 74 0
	ldr	r9, .L666+4
	addw	r5, r5, #1022
	.loc 4 69 0
	ldrh	r2, [r6, #3860]
	movs	r1, #0
.LVL537:
	.loc 4 74 0
	ldr	r10, .L666+8
	.loc 4 69 0
	mov	r0, r7
.LVL538:
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL539:
.L663:
	.loc 4 72 0
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L660
	.loc 4 74 0
	ldrh	r2, [r6, #74]
	cmp	r2, r3
	bhi	.L662
	.loc 4 74 0 is_stmt 0 discriminator 1
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	printk
.LVL540:
.L662:
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
	bne	.L663
.L660:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL541:
.L667:
	.align	2
.L666:
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
	.loc 2 488 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL542:
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI82:
	.loc 2 488 0
	mov	r6, r0
	.loc 2 492 0
	ldr	r4, .L674
	.loc 2 488 0
	mov	r8, r1
	.loc 2 492 0
	ldrh	r3, [r4, #112]
	cmp	r3, r0
	bcs	.L669
	.loc 2 492 0 is_stmt 0 discriminator 1
	mov	r2, #492
	ldr	r1, .L674+4
	ldr	r0, .L674+8
	bl	printk
.LVL543:
.L669:
	.loc 2 493 0 is_stmt 1
	ldr	r3, [r4, #3848]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
.LVL544:
	.loc 2 495 0
	cbnz	r7, .L670
	.loc 2 496 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2468]
	ldrh	r2, [r4, #86]
	movs	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL545:
	.loc 2 497 0
	ldr	r3, [r4, #2468]
	strh	r6, [r3, r5]	@ movhi
	.loc 2 498 0
	ldr	r3, [r4, #2468]
	add	r5, r5, r3
	str	r7, [r5, #4]
.LVL546:
.L671:
	.loc 2 519 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.LVL547:
.L670:
	.loc 2 502 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2468]
	.loc 2 504 0
	movs	r2, #1
	ldr	r0, .L674+12
	mov	r1, r2
	.loc 2 501 0
	str	r7, [r4, #3224]
	.loc 2 502 0
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3228]
	.loc 2 503 0
	ldr	r3, [r4, #3244]
	str	r3, [r4, #3232]
	.loc 2 504 0
	bl	FlashReadPages
.LVL548:
	.loc 2 505 0
	ldr	r8, [r4, #3232]
.LVL549:
	.loc 2 507 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L672
	.loc 2 508 0
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L674+16
	bl	printk
.LVL550:
	.loc 2 509 0
	movs	r3, #4
	ldr	r1, [r4, #3232]
	mov	r2, r3
	ldr	r0, .L674+20
	bl	rknand_print_hex
.LVL551:
	.loc 2 510 0
	ldrh	r3, [r4, #112]
	movs	r2, #4
	ldr	r1, [r4, #3848]
	ldr	r0, .L674+24
	bl	rknand_print_hex
.LVL552:
.L672:
	.loc 2 513 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L673
	.loc 2 513 0 is_stmt 0 discriminator 1
	movw	r2, #513
	ldr	r1, .L674+4
	ldr	r0, .L674+8
	bl	printk
.LVL553:
.L673:
	.loc 2 516 0 is_stmt 1
	ldr	r3, [r4, #2468]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	.loc 2 517 0
	strh	r6, [r3, r5]	@ movhi
	.loc 2 518 0
	b	.L671
.L675:
	.align	2
.L674:
	.word	.LANCHOR0
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0+3220
	.word	.LC82
	.word	.LC83
	.word	.LC84
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
	.loc 2 522 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL554:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI83:
.LVL555:
	.loc 2 522 0
	mov	r4, r0
	.loc 2 531 0
	ldrh	r2, [r0, #10]
	movs	r1, #0
	.loc 2 525 0
	ldr	r5, [r0, #20]
.LVL556:
	.loc 2 523 0
	ldr	r9, [r0, #12]
.LVL557:
	.loc 2 524 0
	ldr	r6, [r0, #24]
.LVL558:
	.loc 2 531 0
	lsls	r2, r2, #1
	mov	r0, r5
.LVL559:
	bl	ftl_memset
.LVL560:
	.loc 2 532 0
	movs	r3, #0
.LVL561:
.L677:
	.loc 2 532 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #6]
.LVL562:
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L681
	.loc 2 540 0 is_stmt 1
	ldrh	fp, [r5]
.LVL563:
	.loc 2 543 0
	movs	r6, #0
.LVL564:
	.loc 2 544 0
	ldr	r2, .L690
	.loc 2 541 0
	mov	r10, r6
.LVL565:
.L682:
	.loc 2 543 0 discriminator 1
	ldrh	r1, [r4, #10]
	uxth	r3, r6
	cmp	r1, r3
	bhi	.L686
	.loc 2 559 0
	mov	r0, r10
	add	sp, sp, #12
.LCFI84:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL566:
.L681:
.LCFI85:
	.loc 2 533 0
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	.loc 2 534 0
	movs	r2, #0
	.loc 2 533 0
	ubfx	r1, r1, #10, #16
.LVL567:
.L678:
	.loc 2 534 0 discriminator 1
	ldrh	r7, [r4, #10]
	uxth	r0, r2
	cmp	r7, r0
	bhi	.L680
	adds	r3, r3, #1
	b	.L677
.L680:
	.loc 2 535 0
	uxth	r0, r2
	adds	r2, r2, #1
	ldrh	r7, [r9, r0, lsl #1]
	cmp	r7, r1
	.loc 2 536 0
	ittt	eq
	ldrheq	r7, [r5, r0, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r5, r0, lsl #1]	@ movhi
	b	.L678
.LVL568:
.L686:
	.loc 2 544 0
	ldrh	r1, [r4]
	uxth	r7, r6
	cmp	r1, r3
	bne	.L683
	.loc 2 544 0 is_stmt 0 discriminator 1
	ldrh	r1, [r2, #80]
	ldrh	r0, [r4, #2]
	cmp	r0, r1
	.loc 2 545 0 is_stmt 1 discriminator 1
	it	cc
	strhcc	r1, [r5, r7, lsl #1]	@ movhi
.L683:
	.loc 2 546 0
	ldrh	r8, [r5, r7, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r3
	movhi	fp, r8
	.loc 2 550 0
	cmp	r8, #0
	bne	.L685
	.loc 2 550 0 is_stmt 0 discriminator 1
	ldrh	r0, [r9, r7, lsl #1]
	cbz	r0, .L685
	.loc 2 552 0 is_stmt 1
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
.LVL569:
	.loc 2 553 0
	strh	r8, [r9, r7, lsl #1]	@ movhi
	.loc 2 554 0
	ldr	r2, [sp, #4]
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L685:
	adds	r6, r6, #1
	b	.L682
.L691:
	.align	2
.L690:
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
	.loc 2 632 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL570:
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI86:
	.loc 2 640 0
	movw	r2, #65535
	ldrh	r3, [r0]
	.loc 2 632 0
	mov	r4, r0
	.loc 2 635 0
	ldr	r5, [r0, #12]
.LVL571:
	.loc 2 640 0
	cmp	r3, r2
	bne	.L693
	.loc 2 641 0
	ldrh	r3, [r0, #8]
	cbz	r3, .L694
	.loc 2 641 0 is_stmt 0 discriminator 1
	movw	r2, #641
	ldr	r1, .L702
	ldr	r0, .L702+4
	bl	printk
.LVL572:
.L694:
	.loc 2 642 0 is_stmt 1
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 643 0
	bl	FtlFreeSysBlkQueueOut
.LVL573:
	.loc 2 644 0
	movs	r3, #0
	.loc 2 643 0
	strh	r0, [r5]	@ movhi
	.loc 2 644 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 645 0
	strh	r3, [r4]	@ movhi
	.loc 2 646 0
	ldr	r3, [r4, #28]
	adds	r3, r3, #1
	str	r3, [r4, #28]
.LVL574:
.L695:
	.loc 2 677 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL575:
.L693:
	.loc 2 650 0
	ldrh	r6, [r5, r3, lsl #1]
	.loc 2 661 0
	movs	r1, #255
	.loc 2 651 0
	ldrh	r3, [r0, #2]
	ldr	r5, .L702+8
.LVL576:
	.loc 2 656 0
	ldr	r2, [r0, #28]
	.loc 2 636 0
	ldr	r7, [r0, #24]
.LVL577:
	.loc 2 651 0
	orr	r3, r3, r6, lsl #10
	str	r3, [r5, #3224]
	.loc 2 652 0
	ldr	r3, [r5, #3240]
	str	r3, [r5, #3228]
	.loc 2 653 0
	ldr	r3, [r5, #3244]
	str	r3, [r5, #3232]
	.loc 2 656 0
	str	r2, [r3, #4]
	.loc 2 657 0
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	.loc 2 658 0
	ldrh	r2, [r0, #4]
	.loc 2 659 0
	strh	r6, [r3, #2]	@ movhi
	.loc 2 658 0
	strh	r2, [r3]	@ movhi
	.loc 2 661 0
	ldrh	r2, [r5, #80]
	ldr	r0, [r5, #3240]
.LVL578:
	lsls	r2, r2, #3
	bl	ftl_memset
.LVL579:
	.loc 2 663 0
	movs	r2, #0
	.loc 2 662 0
	mov	r3, r2
.LVL580:
.L696:
	.loc 2 663 0 discriminator 1
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L698
	.loc 2 670 0
	movs	r2, #1
	movs	r3, #0
.LVL581:
	mov	r1, r2
	ldr	r0, .L702+12
	bl	FlashProgPages
.LVL582:
	.loc 2 671 0
	ldrh	r3, [r4, #2]
	.loc 2 675 0
	mov	r0, r4
	.loc 2 671 0
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	.loc 2 675 0
	bl	ftl_map_blk_gc
.LVL583:
	.loc 2 676 0
	b	.L695
.LVL584:
.L698:
	uxth	r1, r2
	.loc 2 664 0
	ldr	r0, [r7, r1, lsl #2]
	cmp	r6, r0, lsr #10
	bne	.L697
	.loc 2 666 0
	ldr	r0, [r5, #3240]
	.loc 2 665 0
	adds	r3, r3, #1
.LVL585:
	uxth	r3, r3
.LVL586:
	.loc 2 666 0
	str	r1, [r0, r3, lsl #3]
	.loc 2 667 0
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3240]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L697:
	adds	r2, r2, #1
	b	.L696
.L703:
	.align	2
.L702:
	.word	.LANCHOR1+227
	.word	.LC1
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
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
	.loc 2 680 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL587:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI87:
	.loc 2 680 0
	mov	r4, r0
	.loc 2 686 0
	ldr	r7, .L723
	.loc 2 680 0
	mov	r8, r1
	mov	r10, r2
	.loc 2 683 0
	movs	r6, #0
.LVL588:
	.loc 2 699 0
	ldr	r9, .L723+20
	mov	r5, r7
.L705:
	.loc 2 686 0
	ldr	r3, [r7, #2500]
	adds	r3, r3, #1
	str	r3, [r7, #2500]
	.loc 2 687 0
	ldrh	r3, [r7, #80]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L706
	.loc 2 687 0 is_stmt 0 discriminator 1
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L707
.L706:
	.loc 2 688 0 is_stmt 1
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.LVL589:
.L707:
	.loc 2 699 0
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L708
	.loc 2 699 0 is_stmt 0 discriminator 1
	movw	r2, #699
	mov	r1, r9
	ldr	r0, .L723+4
	bl	printk
.LVL590:
.L708:
	.loc 2 700 0 is_stmt 1
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L709
	.loc 2 700 0 is_stmt 0 discriminator 1
	mov	r2, #700
	mov	r1, r9
	ldr	r0, .L723+4
	bl	printk
.LVL591:
.L709:
	.loc 2 701 0 is_stmt 1
	ldrh	r2, [r4]
.LBB385:
.LBB386:
.LBB387:
	.loc 1 202 0
	movs	r1, #16
.LBE387:
.LBE386:
.LBE385:
	.loc 2 701 0
	ldr	r3, [r4, #12]
	.loc 2 705 0
	ldr	r0, [r5, #3244]
	.loc 2 701 0
	ldrh	fp, [r3, r2, lsl #1]
	.loc 2 703 0
	ldrh	r3, [r4, #2]
	.loc 2 705 0
	str	r0, [r5, #3232]
	.loc 2 704 0
	str	r10, [r5, #3228]
	.loc 2 703 0
	orr	r3, r3, fp, lsl #10
	str	r3, [r5, #3224]
.LBB390:
.LBB389:
.LBB388:
	.loc 1 202 0
	bl	__memzero
.LVL592:
.LBE388:
.LBE389:
.LBE390:
	.loc 2 708 0
	ldr	r3, [r5, #3232]
.LVL593:
	.loc 2 709 0
	ldr	r2, [r4, #28]
	.loc 2 713 0
	ldr	r0, .L723+8
	.loc 2 710 0
	strh	r8, [r3, #8]	@ movhi
	.loc 2 709 0
	str	r2, [r3, #4]
	.loc 2 711 0
	ldrh	r2, [r4, #4]
	.loc 2 712 0
	strh	fp, [r3, #2]	@ movhi
	.loc 2 711 0
	strh	r2, [r3]	@ movhi
	.loc 2 713 0
	movs	r3, #1
.LVL594:
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL595:
	.loc 2 714 0
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	.loc 2 716 0
	ldr	r2, [r5, #3220]
	adds	r2, r2, #1
	bne	.L710
	.loc 2 717 0
	ldr	r1, [r5, #3224]
	.loc 2 718 0
	adds	r6, r6, #1
.LVL596:
	.loc 2 717 0
	ldr	r0, .L723+12
	.loc 2 718 0
	uxth	r6, r6
.LVL597:
	.loc 2 717 0
	bl	printk
.LVL598:
	.loc 2 719 0
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	.loc 2 720 0
	ittt	ls
	ldrhls	r3, [r5, #80]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	.loc 2 722 0
	cmp	r6, #3
	bls	.L712
	.loc 2 723 0
	mov	r2, r6
	ldr	r1, [r5, #3224]
	ldr	r0, .L723+16
	bl	printk
.LVL599:
.L713:
	b	.L713
.L712:
	.loc 2 727 0
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L705
.L722:
	b	.L722
.L710:
	.loc 2 731 0
	cmp	r3, #1
	beq	.L705
	.loc 2 734 0
	ldr	r2, [r5, #3224]
	.loc 2 736 0
	movs	r0, #0
	.loc 2 734 0
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	.loc 2 736 0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL600:
.L724:
	.align	2
.L723:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+3220
	.word	.LC85
	.word	.LC86
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
	.loc 2 586 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL601:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI88:
.LVL602:
	.loc 2 586 0
	mov	r4, r0
	.loc 2 590 0
	ldr	r6, [r0, #12]
.LVL603:
	.loc 2 591 0
	ldr	r9, [r0, #24]
.LVL604:
	.loc 2 594 0
	bl	ftl_free_no_use_map_blk
.LVL605:
	.loc 2 596 0
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L737
	subs	r3, r3, #4
	cmp	r2, r3
	blt	.L726
	.loc 2 597 0
	uxth	r0, r0
	ldrh	r8, [r6, r0, lsl #1]
.LVL606:
	.loc 2 598 0
	cmp	r8, #0
	beq	.L726
	.loc 2 598 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #32]
	cbnz	r3, .L726
	.loc 2 599 0 is_stmt 1
	movs	r2, #1
	str	r2, [r4, #32]
	.loc 2 600 0
	strh	r3, [r6, r0, lsl #1]	@ movhi
	.loc 2 601 0
	ldrh	r3, [r4, #8]
	.loc 2 602 0
	ldrh	r2, [r4, #2]
	.loc 2 601 0
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 602 0
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L727
	.loc 2 603 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL607:
.L727:
	.loc 2 613 0 discriminator 1
	ldr	fp, .L737+12
	.loc 2 586 0 discriminator 1
	movs	r6, #0
.LVL608:
.L728:
	.loc 2 605 0 discriminator 1
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L732
	.loc 2 621 0
	movs	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.LVL609:
	.loc 2 622 0
	movs	r3, #0
	str	r3, [r4, #32]
.LVL610:
.L726:
	.loc 2 626 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L733
	.loc 2 627 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL611:
.L733:
	.loc 2 629 0
	movs	r0, #0
	add	sp, sp, #12
.LCFI89:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL612:
.L732:
.LCFI90:
	uxth	r7, r6
	.loc 2 606 0
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L729
	.loc 2 607 0
	ldr	r3, [r5, #3804]
	.loc 2 611 0
	ldr	r0, .L737+4
	.loc 2 607 0
	str	r3, [r5, #3228]
	.loc 2 608 0
	ldr	r3, [r5, #3244]
	str	r3, [r5, #3232]
	.loc 2 609 0
	ldr	r2, [r9, r7, lsl #2]
	.loc 2 608 0
	str	r3, [sp, #4]
	.loc 2 609 0
	str	r2, [r5, #3224]
	.loc 2 611 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL613:
	.loc 2 613 0
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #8]
	cmp	r3, r10
	beq	.L730
	.loc 2 613 0 is_stmt 0 discriminator 1
	movw	r2, #613
	ldr	r1, .L737+8
	mov	r0, fp
	bl	printk
.LVL614:
.L730:
	.loc 2 614 0 is_stmt 1
	ldr	r3, [r5, #3220]
	adds	r3, r3, #1
	bne	.L731
	.loc 2 615 0
	movs	r3, #0
	str	r3, [r9, r7, lsl #2]
.L729:
	adds	r6, r6, #1
	b	.L728
.L731:
	.loc 2 617 0
	ldr	r2, [r5, #3228]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.LVL615:
	b	.L729
.L738:
	.align	2
.L737:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
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
	.loc 2 739 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL616:
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI91:
	.loc 2 742 0
	movs	r4, #12
	ldr	r5, .L740
	muls	r4, r0, r4
	ldr	r3, [r5, #2468]
	.loc 2 743 0
	addw	r0, r5, #3864
.LVL617:
	.loc 2 742 0
	adds	r2, r3, r4
	.loc 2 743 0
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
.LVL618:
	.loc 2 744 0
	ldr	r3, [r5, #2468]
	.loc 2 746 0
	movs	r0, #0
	.loc 2 744 0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	.loc 2 746 0
	pop	{r3, r4, r5, pc}
.L741:
	.align	2
.L740:
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
	.loc 2 802 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL619:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI92:
	.loc 2 802 0
	mov	r7, r1
	.loc 2 805 0
	ldr	r4, .L756
	.loc 2 802 0
	mov	r9, r2
	.loc 2 805 0
	ldrh	r5, [r4, #84]
	adds	r3, r5, #7
	.loc 2 806 0
	movs	r5, #1
	.loc 2 805 0
	lsr	r8, r0, r3
	.loc 2 806 0
	lsls	r5, r5, r3
	.loc 2 808 0
	ldr	r3, [r4, #2480]
	.loc 2 806 0
	subs	r5, r5, #1
	.loc 2 805 0
	uxth	r8, r8
.LVL620:
	.loc 2 806 0
	ands	r5, r5, r0
	.loc 2 808 0
	cmp	r0, r3
	.loc 2 806 0
	uxth	r5, r5
.LVL621:
	.loc 2 808 0
	bcc	.L743
	.loc 2 808 0 is_stmt 0 discriminator 1
	mov	r2, #808
	ldr	r1, .L756+4
	ldr	r0, .L756+8
.LVL622:
	bl	printk
.LVL623:
.L743:
	.loc 2 810 0 is_stmt 1
	ldrh	r2, [r4, #114]
	movs	r3, #0
	.loc 2 811 0
	ldr	r1, [r4, #2468]
	mov	r10, #12
.LVL624:
.L744:
	uxth	r6, r3
	.loc 2 810 0 discriminator 1
	cmp	r6, r2
	bcc	.L749
	.loc 2 825 0
	bl	select_l2p_ram_region
.LVL625:
	.loc 2 826 0
	mul	r10, r10, r0
	ldr	r3, [r4, #2468]
	.loc 2 825 0
	mov	r6, r0
	.loc 2 826 0
	ldrh	r1, [r3, r10]
	add	r2, r3, r10
	movw	r3, #65535
	cmp	r1, r3
	beq	.L750
	.loc 2 826 0 is_stmt 0 discriminator 1
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L750
	.loc 2 827 0 is_stmt 1
	bl	flush_l2p_region
.LVL626:
.L750:
	.loc 2 830 0
	mov	r1, r6
	mov	r0, r8
	bl	load_l2p_region
.LVL627:
	.loc 2 831 0
	b	.L745
.LVL628:
.L749:
	adds	r3, r3, #1
	.loc 2 811 0
	mla	r0, r10, r3, r1
.LVL629:
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L744
.L745:
	movs	r3, #12
	.loc 2 813 0
	cmp	r9, #0
	bne	.L746
	.loc 2 814 0
	ldr	r2, [r4, #2468]
	mla	r3, r3, r6, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r7]
.L747:
	.loc 2 820 0
	ldr	r2, [r4, #2468]
	movs	r3, #12
	mla	r6, r3, r6, r2
.LVL630:
	ldr	r3, [r6, #4]
	adds	r2, r3, #1
	beq	.L753
	.loc 2 821 0
	adds	r3, r3, #1
	str	r3, [r6, #4]
.L753:
	.loc 2 832 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL631:
.L746:
	.loc 2 816 0
	muls	r3, r6, r3
	ldr	r2, [r4, #2468]
	ldr	r1, [r7]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r5, lsl #2]
	.loc 2 817 0
	ldr	r2, [r4, #2468]
	.loc 2 818 0
	strh	r8, [r4, #2472]	@ movhi
	.loc 2 817 0
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L747
.L757:
	.align	2
.L756:
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
	.loc 2 1677 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL632:
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
.LCFI93:
.LVL633:
	.loc 2 1677 0
	mov	r6, r0
	.loc 2 1680 0
	ldr	r5, .L768
.LVL634:
	.loc 2 1678 0
	ubfx	r0, r1, #10, #16
.LVL635:
	.loc 2 1677 0
	str	r1, [sp, #4]
	.loc 2 1678 0
	bl	P2V_block_in_plane
.LVL636:
	.loc 2 1680 0
	ldr	r2, [r5, #2300]
	.loc 2 1678 0
	mov	r7, r0
.LVL637:
	.loc 2 1680 0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L759
	.loc 2 1681 0
	ldr	r4, [r5, #2316]
	cbz	r4, .L760
.LBB391:
	.loc 2 1685 0
	ldr	r1, [r5, #2292]
	.loc 2 1693 0
	mov	ip, #6
	.loc 2 1685 0
	ldr	r2, .L768+4
	.loc 2 1693 0
	movw	lr, #65535
	.loc 2 1684 0
	ldrh	r0, [r5, #2320]
.LVL638:
	.loc 2 1685 0
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.LVL639:
.L761:
	.loc 2 1686 0 discriminator 1
	uxth	r2, r3
	cmp	r0, r2
	bls	.L760
	.loc 2 1687 0
	cmp	r4, r7
	bne	.L762
	.loc 2 1688 0
	mov	r1, r4
	ldr	r0, .L768+8
.LVL640:
	bl	List_remove_node
.LVL641:
	ldrh	r3, [r5, #2320]
	cbnz	r3, .L763
	.loc 2 1688 0 is_stmt 0 discriminator 1
	mov	r2, #1688
	ldr	r1, .L768+12
	ldr	r0, .L768+16
	bl	printk
.LVL642:
.L763:
	.loc 2 1688 0 discriminator 3
	ldrh	r3, [r5, #2320]
	.loc 2 1689 0 is_stmt 1 discriminator 3
	mov	r0, r4
	.loc 2 1688 0 discriminator 3
	subs	r3, r3, #1
	strh	r3, [r5, #2320]	@ movhi
	.loc 2 1689 0 discriminator 3
	bl	INSERT_DATA_LIST
.LVL643:
	.loc 2 1690 0 discriminator 3
	ldr	r2, [r5, #2300]
	ldrh	r3, [r2, r7, lsl #1]
.L759:
.LBE391:
	.loc 2 1699 0
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L760
.LVL644:
.L762:
.LBB392:
	.loc 2 1693 0
	mul	r4, ip, r4
.LVL645:
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
.LVL646:
	cmp	r4, lr
	bne	.L761
.L760:
.LBE392:
	.loc 2 1701 0
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
.LVL647:
	bl	log2phys
.LVL648:
	.loc 2 1702 0
	add	sp, sp, #12
.LCFI94:
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.LVL649:
.L769:
	.align	2
.L768:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2316
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
	.loc 2 2325 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI95:
	.loc 2 2331 0
	movs	r5, #0
.LVL650:
	ldr	r6, .L784
	.loc 2 2325 0
	.pad #32
	sub	sp, sp, #32
.LCFI96:
	.loc 2 2330 0
	movs	r1, #0
	.loc 2 2334 0
	ldr	r8, .L784+16
	mov	r4, r6
	.loc 2 2330 0
	ldr	r0, .L784+4
	bl	printk
.LVL651:
.L771:
	.loc 2 2331 0 discriminator 1
	ldr	r3, [r6, #2480]
	cmp	r5, r3
	bcc	.L777
	.loc 2 2353 0
	add	sp, sp, #32
.LCFI97:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL652:
.L777:
.LCFI98:
	.loc 2 2332 0
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
.LVL653:
	.loc 2 2333 0
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L772
	.loc 2 2334 0
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	printk
.LVL654:
.L772:
	.loc 2 2335 0
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L774
	.loc 2 2336 0
	str	r3, [r4, #3224]
	.loc 2 2340 0
	movs	r2, #0
	.loc 2 2338 0
	ldr	r3, [r4, #3240]
	.loc 2 2342 0
	movs	r1, #1
	.loc 2 2339 0
	ldr	r7, [r4, #3244]
	.loc 2 2342 0
	ldr	r0, .L784+8
	.loc 2 2338 0
	str	r3, [r4, #3228]
	.loc 2 2337 0
	str	r5, [r4, #3236]
	.loc 2 2339 0
	str	r7, [r4, #3232]
	.loc 2 2340 0
	str	r2, [r4, #3220]
	.loc 2 2342 0
	bl	FlashReadPages
.LVL655:
	.loc 2 2343 0
	ldr	r3, [r4, #3220]
	cmp	r3, #256
	beq	.L775
	.loc 2 2343 0 is_stmt 0 discriminator 1
	adds	r3, r3, #1
	beq	.L775
	.loc 2 2344 0 is_stmt 1
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L774
.L775:
	.loc 2 2347 0
	ldr	r2, [r4, #3228]
	ldr	r3, [r4, #3232]
	ldr	r0, .L784+12
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
	ldr	r2, [r4, #3224]
	bl	printk
.LVL656:
.L774:
	.loc 2 2331 0 discriminator 2
	adds	r5, r5, #1
	b	.L771
.L785:
	.align	2
.L784:
	.word	.LANCHOR0
	.word	.LC87
	.word	.LANCHOR0+3220
	.word	.LC89
	.word	.LC88
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
	ldr	r3, .L799
	.loc 5 419 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI99:
	.pad #88
	sub	sp, sp, #88
.LCFI100:
	mov	r4, r3
	.loc 5 425 0
	ldr	r0, [r3, #2692]
	cmp	r0, #0
	beq	.L787
	.loc 5 426 0
	ldr	r1, [r3, #2696]
	ldr	r2, [r3, #2480]
	cmp	r1, r2
	bcs	.L788
	mov	r5, #2048
.LVL657:
.L793:
.LBB398:
.LBB399:
	.loc 5 428 0
	ldr	r0, [r4, #2696]
	ldr	r3, [r4, #2480]
	cmp	r0, r3
	bcs	.L790
	.loc 5 430 0
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
.LVL658:
	.loc 5 432 0
	ldr	r2, [sp]
	.loc 5 431 0
	ldr	r3, [r4, #2696]
	.loc 5 432 0
	adds	r1, r2, #1
	.loc 5 431 0
	add	r3, r3, #1
	str	r3, [r4, #2696]
	.loc 5 432 0
	beq	.L791
.LBB400:
	.loc 5 435 0
	str	r3, [sp, #20]
	.loc 5 438 0
	add	r0, sp, #88
	.loc 5 436 0
	ldr	r3, .L799+4
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
.LVL659:
	.loc 5 440 0
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L790
	.loc 5 442 0
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL660:
	bl	FtlGcRefreshBlock
.LVL661:
.L790:
.LBE400:
	mov	r0, #-1
.L786:
.LBE399:
.LBE398:
	.loc 5 466 0
	add	sp, sp, #88
.LCFI101:
	@ sp needed
	pop	{r4, r5, r6, pc}
.L791:
.LCFI102:
.LBB402:
.LBB401:
	.loc 5 427 0
	subs	r5, r5, #1
	bne	.L793
	b	.L790
.LVL662:
.L788:
.LBE401:
.LBE402:
	.loc 5 451 0
	ldr	r2, [r3, #2484]
	.loc 5 449 0
	movs	r0, #0
	str	r0, [r3, #2692]
	.loc 5 450 0
	str	r0, [r3, #2696]
	.loc 5 451 0
	str	r2, [r3, #2688]
	b	.L786
.L787:
.LBB403:
	.loc 5 456 0
	ldr	r1, [r3, #2484]
	ldr	r6, [r3, #2688]
	add	r2, r1, #1048576
	cmp	r6, r2
	bhi	.L796
	.loc 5 454 0
	ldr	r2, [r3, #2540]
	lsrs	r5, r2, #10
	mov	r2, #33554432
	asrs	r2, r2, r5
.LVL663:
	.loc 5 457 0
	add	r2, r2, r6
.LVL664:
	cmp	r1, r2
	bhi	.L796
	.loc 5 458 0
	ldrb	r3, [r3, #2272]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L786
.L796:
	.loc 5 460 0
	movs	r3, #1
	.loc 5 461 0
	movs	r0, #0
	.loc 5 460 0
	str	r3, [r4, #2692]
	.loc 5 461 0
	str	r0, [r4, #2696]
	.loc 5 462 0
	str	r1, [r4, #2688]
	b	.L786
.L800:
	.align	2
.L799:
	.word	.LANCHOR0
	.word	ftl_temp_buf
.LBE403:
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
	.loc 2 2273 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI103:
	.loc 2 2282 0
	movs	r4, #0
.LVL665:
	ldr	r6, .L825
	.loc 2 2288 0
	ldr	r7, .L825+4
	.loc 2 2279 0
	ldr	r1, .L825+8
	mov	r5, r6
	ldr	r0, .L825+12
	bl	printk
.LVL666:
.LBB410:
.LBB411:
.LBB412:
	.loc 1 202 0
	mov	r1, #8192
	ldr	r0, .L825+4
	bl	__memzero
.LVL667:
.L802:
.LBE412:
.LBE411:
.LBE410:
	.loc 2 2282 0 discriminator 1
	ldr	r3, [r6, #2480]
	cmp	r4, r3
	bcc	.L804
	.loc 2 2295 0 discriminator 1
	ldr	r8, .L825+4
	movs	r4, #0
.LVL668:
	.loc 2 2297 0 discriminator 1
	ldr	r10, .L825+24
	mov	r7, r4
.LVL669:
	.loc 2 2295 0 discriminator 1
	movw	r9, #65535
.L805:
	.loc 2 2293 0 discriminator 1
	ldrh	r2, [r5, #16]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L807
	.loc 2 2305 0
	ldr	r4, [r5, #2316]
	cbz	r4, .L808
.LBB413:
	.loc 2 2308 0
	ldr	r3, [r5, #2292]
	.loc 2 2309 0
	movs	r6, #0
	.loc 2 2307 0
	ldrh	r8, [r5, #2320]
.LVL670:
	.loc 2 2316 0
	mov	fp, #6
	.loc 2 2313 0
	ldr	r9, .L825+4
	.loc 2 2308 0
	subs	r4, r4, r3
	ldr	r3, .L825+16
	asrs	r4, r4, #1
	.loc 2 2313 0
	ldr	r10, .L825+28
	.loc 2 2308 0
	muls	r4, r3, r4
	uxth	r4, r4
.L809:
	.loc 2 2309 0 discriminator 1
	uxth	r3, r6
	cmp	r8, r3
	bls	.L808
	.loc 2 2311 0
	ldr	r3, [r5, #2300]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L810
	.loc 2 2314 0
	movs	r7, #1
	.loc 2 2313 0
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.LVL671:
.L810:
	.loc 2 2316 0
	mul	r4, fp, r4
	ldr	r3, [r5, #2292]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L809
.L808:
.LBE413:
	.loc 2 2321 0
	cbz	r7, .L801
	.loc 2 2321 0 is_stmt 0 discriminator 1
	movw	r2, #2321
	ldr	r1, .L825+8
	ldr	r0, .L825+20
	bl	printk
.LVL672:
.L801:
	.loc 2 2322 0 is_stmt 1
	add	sp, sp, #12
.LCFI104:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL673:
.L804:
.LCFI105:
	.loc 2 2284 0
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
.LVL674:
	.loc 2 2285 0
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L803
	.loc 2 2287 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL675:
	.loc 2 2288 0
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L803:
	.loc 2 2282 0 discriminator 2
	adds	r4, r4, #1
	b	.L802
.LVL676:
.L807:
	.loc 2 2295 0
	ldr	r3, [r5, #2300]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L806
	.loc 2 2297 0
	mov	r1, r6
	mov	r0, r10
	bl	printk
.LVL677:
	.loc 2 2298 0
	ldr	r3, [r5, #2300]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L806
	.loc 2 2298 0 is_stmt 0 discriminator 1
	ldrh	r2, [r8, r6, lsl #1]
	.loc 2 2300 0 is_stmt 1 discriminator 1
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L806:
	adds	r4, r4, #1
	b	.L805
.L826:
	.align	2
.L825:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+314
	.word	.LC90
	.word	-1431655765
	.word	.LC1
	.word	.LC91
	.word	.LC92
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
	.loc 2 946 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL678:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI106:
	.loc 2 946 0
	mov	r6, r0
	.loc 2 952 0
	ldr	r3, [r0, #36]
	cbz	r3, .L827
	.loc 2 948 0
	ldrh	r5, [r0, #6]
	.loc 2 954 0
	movs	r3, #0
	.loc 2 956 0
	ldr	r4, .L831
	.loc 2 949 0
	ldr	r2, [r0, #24]
.LVL679:
	.loc 2 954 0
	str	r3, [r0, #36]
	.loc 2 948 0
	subs	r5, r5, #1
	.loc 2 956 0
	ldr	r0, [r4, #3804]
	.loc 2 979 0
	uxth	r5, r5
	.loc 2 957 0
	ldr	r1, [r4, #3244]
	.loc 2 956 0
	str	r0, [r4, #3228]
	.loc 2 957 0
	str	r1, [r4, #3232]
	.loc 2 979 0
	ldr	r2, [r2, r5, lsl #2]
.LVL680:
	.loc 2 980 0
	str	r2, [r4, #3224]
	.loc 2 981 0
	cbz	r2, .L829
	.loc 2 983 0
	movs	r2, #1
.LVL681:
	addw	r0, r4, #3220
	mov	r1, r2
	bl	FlashReadPages
.LVL682:
.L830:
	.loc 2 990 0
	ldr	r2, [r4, #3228]
	mov	r1, r5
	mov	r0, r6
	.loc 2 992 0
	pop	{r4, r5, r6, lr}
.LCFI107:
.LVL683:
	.loc 2 990 0
	b	FtlMapWritePage
.LVL684:
.L829:
.LCFI108:
	.loc 2 987 0
	ldrh	r2, [r4, #86]
.LVL685:
	movs	r1, #255
	bl	ftl_memset
.LVL686:
	b	.L830
.LVL687:
.L827:
	pop	{r4, r5, r6, pc}
.L832:
	.align	2
.L831:
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
	.loc 2 1144 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI109:
	.loc 2 1150 0
	movs	r5, #0
	ldr	r4, .L914
	.loc 2 1144 0
	.pad #20
	sub	sp, sp, #20
.LCFI110:
	.loc 2 1152 0
	mov	r1, r5
	ldr	r2, [r4, #104]
	ldr	r0, [r4, #3852]
	.loc 2 1150 0
	strh	r5, [r4, #3904]	@ movhi
	.loc 2 1151 0
	strh	r5, [r4, #120]	@ movhi
	.loc 2 1152 0
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL688:
	.loc 2 1153 0
	ldr	r2, [r4, #104]
	mov	r1, r5
	ldr	r0, [r4, #3828]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL689:
	.loc 2 1154 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #3840]
	lsls	r2, r2, #2
	bl	ftl_memset
.LVL690:
	.loc 2 1155 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #124]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL691:
	.loc 2 1158 0
	ldrh	r8, [r4, #16]
.LVL692:
.LBB419:
.LBB420:
.LBB421:
	.loc 1 202 0
	mov	r3, #-1
	str	r3, [r4, #2552]
	str	r3, [r4, #2556]
	str	r3, [r4, #2560]
.LVL693:
.L834:
.LBE421:
.LBE420:
.LBE419:
	.loc 2 1158 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r8
	bls	.L876
	.loc 2 1169 0 discriminator 1
	movs	r5, #0
	.loc 2 1162 0 discriminator 1
	ldrh	fp, [r4, #8]
	.loc 2 1167 0 discriminator 1
	ldr	r9, [r4, #3792]
	.loc 2 1169 0 discriminator 1
	mov	r7, r5
	.loc 2 1168 0 discriminator 1
	ldr	r3, [r4, #3148]
	.loc 2 1167 0 discriminator 1
	mov	r10, #20
	.loc 2 1169 0 discriminator 1
	ldr	r2, [r4, #3152]
	ldrh	r6, [r4, #88]
.LVL694:
	b	.L877
.L837:
	str	r3, [sp, #8]
	.loc 2 1164 0
	mov	r1, r8
	ldr	r3, .L914+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL695:
	str	r0, [sp, #4]
	.loc 2 1165 0
	bl	FtlBbmIsBadBlock
.LVL696:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L835
	.loc 2 1167 0
	ldr	r1, [sp, #4]
	mla	r0, r10, r7, r9
	lsls	r1, r1, #10
	.loc 2 1168 0
	str	r3, [r0, #8]
	.loc 2 1167 0
	str	r1, [r0, #4]
	.loc 2 1169 0
	mov	r1, r6
	muls	r1, r7, r1
	.loc 2 1170 0
	add	r7, r7, #1
.LVL697:
	.loc 2 1169 0
	it	mi
	addmi	r1, r1, #3
	.loc 2 1170 0
	uxth	r7, r7
.LVL698:
	.loc 2 1169 0
	bic	r1, r1, #3
	add	r1, r1, r2
	str	r1, [r0, #12]
.L835:
	adds	r5, r5, #1
.L877:
	.loc 2 1162 0 discriminator 1
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L837
	.loc 2 1173 0
	cbnz	r7, .L838
.LVL699:
.L875:
	.loc 2 1158 0
	add	r8, r8, #1
.LVL700:
	uxth	r8, r8
.LVL701:
	b	.L834
.LVL702:
.L838:
	.loc 2 1176 0
	mov	r0, r9
	.loc 2 1177 0
	mov	r9, #0
	.loc 2 1176 0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.LVL703:
.L839:
	.loc 2 1177 0 discriminator 1
	uxth	r3, r9
	cmp	r7, r3
	bls	.L875
	movs	r3, #20
	mul	r10, r3, r9
	.loc 2 1178 0
	ldr	r3, [r4, #3792]
	add	r2, r3, r10
	.loc 2 1181 0
	ldr	r3, [r3, r10]
	.loc 2 1178 0
	ldr	r5, [r2, #4]
	.loc 2 1179 0
	ldr	r6, [r2, #12]
	.loc 2 1181 0
	adds	r3, r3, #1
	.loc 2 1178 0
	ubfx	r5, r5, #10, #16
	.loc 2 1181 0
	bne	.L842
	mov	fp, #16
	.loc 2 1185 0
	movw	r3, #65535
.L844:
	.loc 2 1183 0
	ldr	r0, [r4, #3792]
	str	r3, [sp, #4]
	add	r0, r0, r10
	ldr	r2, [r0, #4]
	adds	r2, r2, #1
	str	r2, [r0, #4]
	.loc 2 1184 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL704:
	.loc 2 1185 0
	ldrh	r2, [r6]
	ldr	r3, [sp, #4]
	cmp	r2, r3
	bne	.L841
	.loc 2 1186 0
	ldr	r3, [r4, #3792]
	mov	r2, #-1
	str	r2, [r3, r10]
	.loc 2 1194 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L842
.L843:
	.loc 2 1336 0
	movs	r1, #0
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
.LVL705:
	b	.L847
.L841:
	.loc 2 1188 0
	ldr	r2, [r4, #3792]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L842
	add	fp, fp, #-1
	uxth	fp, fp
	.loc 2 1182 0 discriminator 2
	cmp	fp, #0
	bne	.L844
	b	.L843
.L842:
	.loc 2 1196 0
	ldr	r2, [r4, #2516]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L845
	.loc 2 1196 0 is_stmt 0 discriminator 1
	cmp	r2, r3
	bhi	.L846
.L845:
	.loc 2 1198 0 is_stmt 1
	adds	r2, r3, #1
	.loc 2 1201 0
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2516]
.L846:
	.loc 2 1206 0
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L848
	bhi	.L849
	movw	r3, #61574
	cmp	r2, r3
	beq	.L850
.L847:
	add	r9, r9, #1
	b	.L839
.L849:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L851
	movw	r3, #65535
	cmp	r2, r3
	beq	.L843
	b	.L847
.L851:
	.loc 2 1211 0
	ldrh	r2, [r4, #3904]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L853
	.loc 2 1211 0 is_stmt 0 discriminator 1
	movw	r2, #1211
	ldr	r1, .L914+8
	ldr	r0, .L914+12
	bl	printk
.LVL706:
.L853:
	.loc 2 1212 0 is_stmt 1
	ldr	r1, [r4, #104]
	ldrh	r0, [r4, #3904]
	.loc 2 1214 0
	ldr	r10, [r4, #3852]
	.loc 2 1212 0
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L854:
	.loc 2 1212 0 is_stmt 0 discriminator 1
	cmp	r3, ip
	bgt	.L860
	.loc 2 1235 0 is_stmt 1
	cmp	r3, #0
	bge	.L892
	b	.L847
.L860:
	.loc 2 1214 0
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L855
	.loc 2 1216 0
	ldr	r2, [r10]
	cbnz	r2, .L856
	.loc 2 1216 0 is_stmt 0 discriminator 1
	cmp	r1, r0
	.loc 2 1221 0 is_stmt 1 discriminator 1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3904]	@ movhi
.L856:
	.loc 2 1224 0 discriminator 1
	uxth	ip, r3
	movs	r2, #0
.L857:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L858
	.loc 2 1229 0
	ldr	r2, [r4, #3852]
	.loc 2 1235 0
	cmp	r3, #0
	.loc 2 1229 0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	.loc 2 1230 0
	ldr	r2, [r4, #3828]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	.loc 2 1235 0
	blt	.L847
	.loc 2 1242 0
	ldrh	r0, [r4, #3904]
	ldr	r2, [r4, #104]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L847
.L892:
	.loc 2 1245 0
	ldr	r2, [r4, #3852]
	.loc 2 1244 0
	adds	r0, r0, #1
	.loc 2 1245 0
	ldr	r1, [r6, #4]
	.loc 2 1244 0
	strh	r0, [r4, #3904]	@ movhi
	.loc 2 1245 0
	str	r1, [r2, r3, lsl #2]
	.loc 2 1246 0
	ldr	r2, [r4, #3828]
.L912:
	.loc 2 1290 0
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L847
.L858:
	.loc 2 1226 0 discriminator 3
	ldr	r0, [r4, #3852]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	.loc 2 1227 0 discriminator 3
	ldr	r0, [r4, #3828]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L857
.L855:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L854
.L915:
	.align	2
.L914:
	.word	.LANCHOR0
	.word	.LANCHOR0+36
	.word	.LANCHOR1+328
	.word	.LC1
.L850:
	.loc 2 1252 0
	ldrh	r2, [r4, #120]
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bls	.L863
	.loc 2 1252 0 is_stmt 0 discriminator 1
	movw	r2, #1252
	ldr	r1, .L916
	ldr	r0, .L916+4
	bl	printk
.LVL707:
.L863:
	.loc 2 1253 0 is_stmt 1
	ldrh	lr, [r4, #96]
	ldrh	r0, [r4, #120]
	.loc 2 1255 0
	ldr	r10, [r4, #3840]
	.loc 2 1253 0
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L864:
	.loc 2 1253 0 is_stmt 0 discriminator 1
	cmp	r3, ip
	ble	.L869
	.loc 2 1255 0 is_stmt 1
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L865
	.loc 2 1257 0
	ldr	r2, [r10]
	cbnz	r2, .L866
	.loc 2 1257 0 is_stmt 0 discriminator 1
	cmp	lr, r0
	.loc 2 1265 0 is_stmt 1 discriminator 1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #120]	@ movhi
.L866:
	.loc 2 1267 0 discriminator 1
	uxth	ip, r3
	movs	r2, #0
.L867:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L868
	.loc 2 1272 0
	ldr	r2, [r4, #3840]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	.loc 2 1273 0
	ldr	r2, [r4, #124]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L869:
	.loc 2 1278 0
	cmp	r3, #0
	blt	.L847
	.loc 2 1286 0
	ldrh	r2, [r4, #96]
	ldrh	r1, [r4, #120]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L847
	.loc 2 1288 0
	adds	r1, r1, #1
	.loc 2 1289 0
	ldr	r2, [r4, #3840]
	.loc 2 1288 0
	strh	r1, [r4, #120]	@ movhi
	.loc 2 1289 0
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	.loc 2 1290 0
	ldr	r2, [r4, #124]
	b	.L912
.L868:
	.loc 2 1269 0 discriminator 3
	ldr	r0, [r4, #3840]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	.loc 2 1270 0 discriminator 3
	ldr	r0, [r4, #124]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L867
.L865:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L864
.L848:
	.loc 2 1297 0
	ldrh	r1, [r4, #2552]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L871
	.loc 2 1299 0
	strh	r5, [r4, #2552]	@ movhi
.L913:
	.loc 2 1310 0
	str	r3, [r4, #2560]
	b	.L847
.L871:
	.loc 2 1304 0
	ldrh	r0, [r4, #2556]
	cmp	r0, r2
	beq	.L872
	.loc 2 1305 0
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.LVL708:
.L872:
	.loc 2 1306 0
	ldr	r2, [r4, #2560]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L873
	.loc 2 1308 0
	ldrh	r3, [r4, #2552]
	.loc 2 1309 0
	strh	r5, [r4, #2552]	@ movhi
	.loc 2 1308 0
	strh	r3, [r4, #2556]	@ movhi
	.loc 2 1310 0
	ldr	r3, [r6, #4]
	b	.L913
.L873:
	.loc 2 1314 0
	strh	r5, [r4, #2556]	@ movhi
	b	.L847
.L876:
	.loc 2 1341 0
	ldr	r1, [r4, #3828]
	ldrh	r3, [r1]
	cbz	r3, .L878
.L881:
	.loc 2 1359 0
	ldr	r1, [r4, #124]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L879
.L880:
	.loc 2 1377 0
	ldrh	r2, [r4, #3904]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L911
	.loc 2 1377 0 is_stmt 0 discriminator 1
	movw	r2, #1377
	ldr	r1, .L916
	ldr	r0, .L916+4
	bl	printk
.LVL709:
.L911:
	.loc 2 1379 0 is_stmt 1
	movs	r0, #0
	add	sp, sp, #20
.LCFI111:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL710:
.L878:
.LCFI112:
	.loc 2 1341 0 discriminator 1
	ldrh	r2, [r4, #3904]
	cmp	r2, #0
	beq	.L881
	.loc 2 1343 0 discriminator 1
	ldr	r0, [r4, #104]
.L882:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L881
	.loc 2 1345 0
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L882
	mov	r3, r2
	.loc 2 1351 0 discriminator 1
	movs	r5, #0
.L883:
	.loc 2 1347 0 discriminator 1
	ldr	r1, [r4, #104]
	cmp	r3, r1
	bcs	.L881
	.loc 2 1349 0 discriminator 3
	ldr	r1, [r4, #3828]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
.LVL711:
	strh	r6, [r1, r0, lsl #1]	@ movhi
	.loc 2 1350 0 discriminator 3
	ldr	r1, [r4, #3852]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	.loc 2 1351 0 discriminator 3
	ldr	r1, [r4, #3828]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L883
.LVL712:
.L879:
	.loc 2 1359 0 discriminator 1
	ldrh	r3, [r4, #120]
	cmp	r3, #0
	beq	.L880
	.loc 2 1361 0 discriminator 1
	ldrh	r0, [r4, #96]
.L888:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L880
	.loc 2 1363 0
	ldrh	r6, [r1, r3, lsl #1]
.LVL713:
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L888
	.loc 2 1369 0 discriminator 1
	movs	r0, #0
.L889:
	.loc 2 1365 0 discriminator 1
	ldrh	r2, [r4, #96]
	cmp	r3, r2
	bge	.L880
	.loc 2 1367 0 discriminator 3
	ldr	r2, [r4, #124]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	.loc 2 1368 0 discriminator 3
	ldr	r2, [r4, #3840]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	.loc 2 1369 0 discriminator 3
	ldr	r2, [r4, #124]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L889
.L917:
	.align	2
.L916:
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
	.loc 2 1515 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL714:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI113:
	.pad #28
	sub	sp, sp, #28
.LCFI114:
	.loc 2 1519 0
	ldr	r3, [r0, #24]
.LVL715:
	.loc 2 1515 0
	mov	r4, r0
	.loc 2 1526 0
	movs	r1, #0
	.loc 2 1522 0
	ldrh	r10, [r0, #6]
.LVL716:
	.loc 2 1519 0
	str	r3, [sp, #4]
	.loc 2 1521 0
	ldr	r3, [r0, #16]
.LVL717:
	.loc 2 1527 0
	ldr	r5, .L945
	.loc 2 1520 0
	ldr	r9, [r0, #12]
.LVL718:
	.loc 2 1526 0
	lsl	r2, r10, #2
	.loc 2 1521 0
	str	r3, [sp, #12]
	.loc 2 1523 0
	ldrh	r3, [r0, #8]
.LVL719:
	.loc 2 1526 0
	ldr	r0, [sp, #4]
.LVL720:
	.loc 2 1523 0
	str	r3, [sp, #8]
	.loc 2 1526 0
	bl	ftl_memset
.LVL721:
	.loc 2 1527 0
	ldr	r3, [r5, #3240]
	.loc 2 1534 0
	movs	r2, #1
	.loc 2 1528 0
	ldr	r6, [r5, #3244]
	.loc 2 1534 0
	str	r2, [r4, #36]
	.loc 2 1527 0
	str	r3, [r5, #3228]
	.loc 2 1530 0
	movw	r3, #65535
	.loc 2 1528 0
	str	r6, [r5, #3232]
	.loc 2 1530 0
	strh	r3, [r4]	@ movhi
	.loc 2 1531 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 1532 0
	movs	r3, #0
	str	r3, [r4, #32]
	.loc 2 1535 0
	mov	r8, r3
	.loc 2 1533 0
	str	r3, [r4, #28]
	.loc 2 1537 0
	ldr	r3, [sp, #8]
.LVL722:
	add	fp, r3, #-1
	.loc 2 1579 0
	mov	r3, r5
.LVL723:
.L919:
	.loc 2 1535 0 discriminator 1
	ldr	r1, [sp, #8]
.LVL724:
	sxth	r2, r8
.LVL725:
	cmp	r2, r1
	bge	.L936
	.loc 2 1537 0
	cmp	r2, fp
	lsl	r7, r2, #1
.LVL726:
	bne	.L920
	.loc 2 1539 0
	ldrh	r0, [r9, fp, lsl #1]
.LVL727:
	movs	r1, #1
.LVL728:
	bl	FtlGetLastWrittenPage
.LVL729:
	.loc 2 1542 0
	ldr	r3, [sp, #12]
.LVL730:
	.loc 2 1539 0
	add	r7, r7, r9
	.loc 2 1541 0
	strh	r8, [r4]	@ movhi
	.loc 2 1539 0
	sxth	r9, r0
.LVL731:
	.loc 2 1546 0
	ldr	r8, .L945+4
	.loc 2 1540 0
	adds	r0, r0, #1
	strh	r0, [r4, #2]	@ movhi
	.loc 2 1543 0
	add	r9, r9, #1
.LVL732:
	.loc 2 1542 0
	ldr	r3, [r3, fp, lsl #2]
.LVL733:
	.loc 2 1543 0
	mov	fp, #0
	.loc 2 1542 0
	str	r3, [r4, #28]
.LVL734:
.L921:
	.loc 2 1543 0 discriminator 1
	sxth	r2, fp
	cmp	r2, r9
	blt	.L923
.LVL735:
.L936:
	.loc 2 1596 0
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
.LVL736:
	.loc 2 1597 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bne	.L925
	.loc 2 1599 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL737:
.L925:
	.loc 2 1602 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL738:
	.loc 2 1604 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL739:
	.loc 2 1606 0
	movs	r0, #0
	add	sp, sp, #28
.LCFI115:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL740:
.L923:
.LCFI116:
	.loc 2 1545 0
	ldrh	r1, [r7]
	.loc 2 1546 0
	mov	r0, r8
	.loc 2 1545 0
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3224]
	.loc 2 1546 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL741:
	.loc 2 1549 0
	ldr	r2, [r5, #3220]
	adds	r2, r2, #1
	beq	.L922
	.loc 2 1551 0
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L922
	.loc 2 1551 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	.loc 2 1553 0 is_stmt 1 discriminator 1
	ittt	eq
	ldreq	r1, [r5, #3224]
	ldreq	r3, [sp, #4]
.LVL742:
	streq	r1, [r3, r2, lsl #2]
.L922:
	add	fp, fp, #1
	b	.L921
.LVL743:
.L920:
	.loc 2 1559 0
	ldr	r1, [r5, #3240]
.LVL744:
	str	r3, [sp, #20]
	.loc 2 1560 0
	add	r3, r9, r7
	.loc 2 1561 0
	ldr	r0, .L945+4
.LVL745:
	.loc 2 1559 0
	str	r1, [r5, #3228]
	.loc 2 1560 0
	ldrh	r1, [r9, r2, lsl #1]
	ldrh	r2, [r5, #80]
.LVL746:
	str	r3, [sp, #16]
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3224]
	.loc 2 1561 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL747:
	.loc 2 1564 0
	ldr	r2, [r5, #3220]
	ldr	r3, [sp, #20]
	adds	r2, r2, #1
	beq	.L938
	.loc 2 1564 0 is_stmt 0 discriminator 1
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L938
	.loc 2 1564 0 discriminator 2
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L927
.L938:
	movs	r7, #0
.LVL748:
.L928:
	.loc 2 1579 0 is_stmt 1 discriminator 1
	ldrh	r1, [r3, #80]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L934
	.loc 2 1581 0
	ldr	r1, [sp, #16]
	.loc 2 1582 0
	ldr	r0, .L945+4
.LVL749:
	.loc 2 1581 0
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	str	r2, [r3, #3224]
	.loc 2 1582 0
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL750:
	.loc 2 1585 0
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #3220]
	adds	r2, r2, #1
	beq	.L932
	.loc 2 1587 0
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L932
	.loc 2 1587 0 is_stmt 0 discriminator 1
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	.loc 2 1589 0 is_stmt 1 discriminator 1
	ittt	eq
	ldreq	r1, [r3, #3224]
	ldreq	r0, [sp, #4]
.LVL751:
	streq	r1, [r0, r2, lsl #2]
.L932:
	adds	r7, r7, #1
	b	.L928
.LVL752:
.L927:
	movs	r0, #0
	.loc 2 1572 0 discriminator 1
	mov	ip, #4
.LVL753:
.L929:
	.loc 2 1567 0 discriminator 1
	ldrh	r1, [r5, #80]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L931
.L934:
	add	r8, r8, #1
	b	.L919
.L931:
	.loc 2 1569 0
	ldr	r7, [r5, #3240]
.LVL754:
	ldr	r1, [r7, r2, lsl #3]
	.loc 2 1570 0
	uxth	lr, r1
	cmp	r10, lr
	bls	.L930
	.loc 2 1572 0
	add	r2, ip, r2, lsl #3
	ldr	r2, [r7, r2]
	ldr	r7, [sp, #4]
.LVL755:
	str	r2, [r7, lr, lsl #2]
.L930:
	adds	r0, r0, #1
	b	.L929
.L946:
	.align	2
.L945:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
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
	.loc 2 1609 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	.loc 2 1610 0
	ldr	r0, .L948
	.loc 2 1609 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI117:
	.loc 2 1610 0
	ldrh	r3, [r0, #96]
	strh	r3, [r0, #3918]	@ movhi
	.loc 2 1611 0
	movw	r3, #61574
	strh	r3, [r0, #3912]	@ movhi
	.loc 2 1612 0
	ldrh	r3, [r0, #120]
	strh	r3, [r0, #3916]	@ movhi
	.loc 2 1613 0
	ldrh	r3, [r0, #98]
	strh	r3, [r0, #3914]	@ movhi
	.loc 2 1614 0
	ldr	r3, [r0, #124]
	.loc 2 1619 0
	addw	r0, r0, #3908
	.loc 2 1614 0
	str	r3, [r0, #12]
	.loc 2 1615 0
	ldr	r3, [r0, #-68]
	str	r3, [r0, #16]
	.loc 2 1616 0
	ldr	r3, [r0, #-72]
	str	r3, [r0, #20]
	.loc 2 1617 0
	ldr	r3, [r0, #-64]
	str	r3, [r0, #24]
	.loc 2 1619 0
	bl	FtlMapTblRecovery
.LVL756:
	.loc 2 1622 0
	movs	r0, #0
	pop	{r3, pc}
.L949:
	.align	2
.L948:
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
	.loc 2 1625 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI118:
	.loc 2 1627 0
	movs	r1, #0
	ldr	r4, .L953
	.loc 2 1631 0
	movs	r6, #12
	.loc 2 1627 0
	ldr	r2, [r4, #104]
	ldr	r0, [r4, #3832]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL757:
	.loc 2 1628 0
	ldrh	r3, [r4, #86]
	movs	r1, #255
	ldrh	r2, [r4, #114]
	ldr	r0, [r4, #3856]
	muls	r2, r3, r2
	bl	ftl_memset
.LVL758:
	.loc 2 1629 0
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	.loc 2 1631 0
	mov	r7, r2
.L951:
	.loc 2 1629 0 discriminator 1
	ldrh	r4, [r3, #114]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L952
	.loc 2 1637 0
	ldr	r2, [r3, #104]
	.loc 2 1635 0
	strh	r1, [r3, #3866]	@ movhi
	.loc 2 1636 0
	strh	r1, [r3, #3864]	@ movhi
	.loc 2 1637 0
	strh	r2, [r3, #3874]	@ movhi
	.loc 2 1638 0
	movw	r2, #61634
	strh	r2, [r3, #3868]	@ movhi
	.loc 2 1639 0
	ldrh	r2, [r3, #3904]
	strh	r2, [r3, #3872]	@ movhi
	.loc 2 1640 0
	ldrh	r2, [r3, #112]
	strh	r2, [r3, #3870]	@ movhi
	.loc 2 1641 0
	ldr	r2, [r3, #3828]
	str	r2, [r3, #3876]
	.loc 2 1642 0
	ldr	r2, [r3, #3852]
	str	r2, [r3, #3880]
	.loc 2 1643 0
	ldr	r2, [r3, #3832]
	str	r2, [r3, #3884]
	.loc 2 1644 0
	ldr	r2, [r3, #3848]
	str	r2, [r3, #3888]
	pop	{r3, r4, r5, r6, r7, pc}
.L952:
	.loc 2 1631 0 discriminator 3
	uxth	r2, r2
	ldr	r0, [r3, #2468]
	mul	r4, r6, r2
	add	ip, r0, r4
	str	r7, [ip, #4]
	.loc 2 1632 0 discriminator 3
	strh	r1, [r0, r4]	@ movhi
	.loc 2 1633 0 discriminator 3
	ldr	r0, [r3, #2468]
	add	r0, r0, r4
	ldrh	r4, [r3, #86]
	muls	r2, r4, r2
	ldr	r4, [r3, #3856]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L951
.L954:
	.align	2
.L953:
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
	.loc 2 1648 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI119:
	.loc 2 1649 0
	bl	FtlL2PDataInit
.LVL759:
	.loc 2 1651 0
	ldr	r0, .L956
	bl	FtlMapTblRecovery
.LVL760:
	.loc 2 1654 0
	movs	r0, #0
	pop	{r3, pc}
.L957:
	.align	2
.L956:
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
.LCFI120:
	.loc 3 496 0
	movs	r5, #0
	ldr	r4, .L959
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
.LVL761:
	.loc 3 503 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #2304]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL762:
	.loc 3 504 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #3816]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL763:
.LBB432:
.LBB433:
.LBB434:
	.loc 1 202 0
	addw	r0, r4, #2244
	movs	r1, #48
	bl	__memzero
.LVL764:
.LBE434:
.LBE433:
.LBE432:
.LBB435:
.LBB436:
.LBB437:
	addw	r0, r4, #2612
	mov	r1, #512
	bl	__memzero
.LVL765:
.LBE437:
.LBE436:
.LBE435:
	.loc 3 507 0
	bl	FtlGcBufInit
.LVL766:
	.loc 3 508 0
	bl	FtlL2PDataInit
.LVL767:
	.loc 3 510 0
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L960:
	.align	2
.L959:
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
	.loc 2 2116 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI121:
	.loc 2 2121 0
	movs	r6, #0
	.loc 2 2120 0
	ldr	r4, .L972
	movs	r2, #6
	.loc 2 2126 0
	mov	r9, r6
.LVL768:
	mov	r7, r6
.LVL769:
	.loc 2 2120 0
	movs	r1, #0
	ldrh	r3, [r4, #18]
	.loc 2 2131 0
	add	fp, r4, #36
	.loc 2 2120 0
	ldr	r0, [r4, #2292]
	muls	r2, r3, r2
	bl	ftl_memset
.LVL770:
	.loc 2 2121 0
	str	r6, [r4, #2316]
	.loc 2 2122 0
	str	r6, [r4, #2296]
	.loc 2 2123 0
	str	r6, [r4, #2308]
	.loc 2 2124 0
	strh	r6, [r4, #2312]	@ movhi
	.loc 2 2125 0
	strh	r6, [r4, #2320]	@ movhi
.LVL771:
.L962:
	.loc 2 2128 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r8, r6
	cmp	r8, r3
	bcs	.L968
	.loc 2 2133 0
	mov	r10, #0
	.loc 2 2130 0
	ldrh	r2, [r4, #8]
	.loc 2 2133 0
	ldrh	r3, [r4, #78]
	mov	r5, r10
	b	.L969
.L964:
	.loc 2 2131 0
	mov	r1, r8
	ldrb	r0, [fp, r10]	@ zero_extendqisi2
	stm	sp, {r2, r3}
	bl	V2P_block
.LVL772:
	.loc 2 2132 0
	bl	FtlBbmIsBadBlock
.LVL773:
	ldm	sp, {r2, r3}
	cbnz	r0, .L963
	.loc 2 2133 0
	add	r5, r5, r3
	uxth	r5, r5
.L963:
	add	r10, r10, #1
.L969:
	.loc 2 2130 0 discriminator 1
	uxth	r1, r10
	cmp	r2, r1
	bhi	.L964
	.loc 2 2135 0
	cbz	r5, .L971
	.loc 2 2136 0
	mov	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
.LVL774:
.L965:
	.loc 2 2137 0
	ldr	r2, [r4, #2292]
	uxth	r1, r6
	movs	r3, #6
	mla	r3, r3, r1, r2
	strh	r0, [r3, #4]	@ movhi
	.loc 2 2139 0
	ldrh	r3, [r4, #2324]
	cmp	r3, r8
	beq	.L966
	.loc 2 2139 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2372]
	cmp	r3, r8
	beq	.L966
	.loc 2 2140 0 is_stmt 1
	ldrh	r3, [r4, #2420]
	cmp	r3, r8
	beq	.L966
	.loc 2 2143 0
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r1, lsl #1]
	cbnz	r3, .L967
	.loc 2 2144 0
	add	r9, r9, #1
.LVL775:
	.loc 2 2145 0
	mov	r0, r8
	.loc 2 2144 0
	uxth	r9, r9
.LVL776:
	.loc 2 2145 0
	bl	INSERT_FREE_LIST
.LVL777:
.L966:
	adds	r6, r6, #1
	b	.L962
.L971:
	mov	r0, r5
	b	.L965
.L967:
	.loc 2 2147 0
	adds	r7, r7, #1
.LVL778:
	.loc 2 2148 0
	mov	r0, r8
	.loc 2 2147 0
	uxth	r7, r7
.LVL779:
	.loc 2 2148 0
	bl	INSERT_DATA_LIST
.LVL780:
	b	.L966
.L968:
	.loc 2 2152 0
	strh	r7, [r4, #2312]	@ movhi
	.loc 2 2154 0
	add	r7, r7, r9
.LVL781:
	cmp	r7, r3
	.loc 2 2153 0
	strh	r9, [r4, #2320]	@ movhi
	.loc 2 2154 0
	ble	.L970
	.loc 2 2154 0 is_stmt 0 discriminator 1
	movw	r2, #2154
	ldr	r1, .L972+4
	ldr	r0, .L972+8
	bl	printk
.LVL782:
.L970:
	.loc 2 2156 0 is_stmt 1
	movs	r0, #0
	add	sp, sp, #12
.LCFI122:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL783:
.L973:
	.align	2
.L972:
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
.LCFI123:
	.loc 5 203 0
	movs	r3, #0
	ldr	r4, .L975
	.loc 5 205 0
	movs	r1, #255
	ldrh	r2, [r4, #82]
	ldr	r0, [r4, #3164]
	.loc 5 203 0
	strh	r3, [r4, #3168]	@ movhi
	.loc 5 204 0
	strh	r3, [r4, #3176]	@ movhi
	.loc 5 205 0
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL784:
	.loc 5 206 0
	ldrh	r3, [r4, #82]
	movs	r2, #12
	ldr	r0, [r4, #3172]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
.LVL785:
	.loc 5 208 0
	pop	{r4, lr}
.LCFI124:
	.loc 5 207 0
	b	FtlGcBufInit
.LVL786:
.L976:
	.align	2
.L975:
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
.LVL787:
	.loc 5 220 0
	ldr	r3, .L1002
	.loc 5 211 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI125:
	.pad #28
	sub	sp, sp, #28
.LCFI126:
	.loc 5 211 0
	mov	r7, r0
	str	r1, [sp, #8]
	.loc 5 220 0
	ldrh	r4, [r3, #4]
.LVL788:
	.loc 5 221 0
	movw	r3, #65535
	cmp	r4, r3
	beq	.L996
	.loc 5 223 0
	cbnz	r4, .L978
.LVL789:
.L979:
	.loc 5 224 0
	bl	FtlGcPageVarInit
.LVL790:
	b	.L980
.LVL791:
.L996:
	.loc 5 222 0
	movs	r4, #0
.L978:
	.loc 5 223 0 discriminator 1
	ldr	r3, .L1002+4
	ldr	r2, [sp, #8]
.LVL792:
	ldrh	r3, [r3, #78]
	cmp	r3, r2
	beq	.L979
.L980:
	.loc 5 222 0
	mov	fp, #0
.LVL793:
	.loc 5 228 0
	movw	r8, #65535
.LVL794:
.L990:
	ldrh	r3, [r7]
	.loc 5 226 0
	movs	r0, #0
	strb	r0, [r7, #8]
	.loc 5 228 0
	cmp	r3, r8
	beq	.L981
	.loc 5 231 0
	ldr	r5, .L1002+4
.L993:
	.loc 5 234 0
	ldr	r3, [r5, #3792]
	.loc 5 231 0
	movs	r2, #0
.LVL795:
	ldrh	ip, [r5, #8]
	add	lr, r7, #16
	.loc 5 236 0
	ldr	r10, [r5, #3152]
	.loc 5 230 0
	mov	r6, r2
	.loc 5 234 0
	str	r3, [sp, #4]
	mov	r9, #20
	.loc 5 235 0
	ldr	r3, [r5, #3148]
	str	r3, [sp, #12]
	ldrh	r3, [r5, #86]
	str	r3, [sp, #16]
	.loc 5 236 0
	ldrh	r3, [r5, #88]
	str	r3, [sp, #20]
.LVL796:
.L982:
	.loc 5 231 0 discriminator 1
	uxth	r3, r2
.LVL797:
	cmp	ip, r3
	bhi	.L986
	.loc 5 241 0
	mov	r9, #0
	.loc 5 240 0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [sp, #4]
	bl	FlashReadPages
.LVL798:
.L987:
	.loc 5 241 0 discriminator 1
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L991
	.loc 5 268 0
	ldr	r3, [sp, #8]
.LVL799:
	.loc 5 266 0
	add	fp, fp, #1
	.loc 5 264 0
	adds	r4, r4, #1
.LVL800:
	uxth	r4, r4
.LVL801:
	.loc 5 268 0
	cmp	r3, fp
	bls	.L992
.L994:
	.loc 5 274 0
	ldrh	r3, [r5, #78]
.LVL802:
	cmp	r3, r4
	bhi	.L993
	.loc 5 265 0
	movs	r0, #0
.LVL803:
	b	.L981
.LVL804:
.L986:
	.loc 5 232 0
	ldrh	r3, [lr], #2
.LVL805:
	.loc 5 233 0
	cmp	r3, r8
	beq	.L983
	.loc 5 234 0
	ldr	r1, [sp, #4]
	orr	r3, r4, r3, lsl #10
.LVL806:
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
.LVL807:
	.loc 5 236 0
	it	mi
	addmi	r3, r3, #3
	.loc 5 237 0
	uxth	r6, r6
.LVL808:
	.loc 5 236 0
	bic	r3, r3, #3
	.loc 5 235 0
	str	r0, [r1, #8]
	.loc 5 236 0
	add	r3, r3, r10
	str	r3, [r1, #12]
.LVL809:
.L983:
	adds	r2, r2, #1
	b	.L982
.LVL810:
.L991:
	.loc 5 242 0
	movs	r3, #20
	ldr	r2, [r5, #3792]
	mul	r10, r3, r9
	str	r2, [sp, #16]
	add	r3, r2, r10
	ldr	r1, [r3, #4]
.LVL811:
	str	r3, [sp, #12]
	.loc 5 243 0
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_plane
.LVL812:
	.loc 5 245 0
	ldr	r2, [sp, #16]
	ldr	r2, [r2, r10]
	cbnz	r2, .L988
	.loc 5 247 0
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	.loc 5 248 0
	ldr	r1, [sp, #4]
.LVL813:
	.loc 5 247 0
	ldr	r3, [r3, #12]
.LVL814:
	.loc 5 248 0
	ldrh	r2, [r3]
	cmp	r2, r8
	bne	.L989
	.loc 5 249 0
	movs	r3, #1
.LVL815:
	str	r3, [r5, #3960]
.LVL816:
.L981:
	.loc 5 281 0
	ldr	r3, .L1002
	movw	r2, #65535
.LVL817:
	.loc 5 283 0
	strb	r0, [r7, #6]
	.loc 5 284 0
	mov	r1, r4
.LVL818:
	.loc 5 282 0
	strh	r4, [r7, #2]	@ movhi
	.loc 5 281 0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 284 0
	mov	r2, r0
	mov	r0, r7
.LVL819:
	bl	ftl_sb_update_avl_pages
.LVL820:
	.loc 5 285 0
	b	.L995
.LVL821:
.L989:
	.loc 5 252 0
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL822:
	b	.L987
.L988:
	.loc 5 256 0
	ldrh	r2, [r7]
	movs	r4, #0
.LVL823:
	ldr	r3, [r5, #2300]
	strh	r4, [r3, r2, lsl #1]	@ movhi
	.loc 5 257 0
	ldrh	r0, [r7]
	bl	INSERT_FREE_LIST
.LVL824:
	.loc 5 258 0
	strh	r8, [r7]	@ movhi
	.loc 5 260 0
	bl	FtlGcPageVarInit
.LVL825:
	.loc 5 261 0
	b	.L990
.LVL826:
.L992:
	.loc 5 268 0 discriminator 1
	ldr	r2, .L1002
	ldrh	r3, [r2, #4]
.LVL827:
	cmp	r3, r8
	beq	.L994
	.loc 5 269 0
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	.loc 5 270 0
	ldrh	r3, [r5, #78]
	cmp	r3, r4
	bls	.L994
.L995:
	.loc 5 286 0
	mov	r0, #-1
	add	sp, sp, #28
.LCFI127:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL828:
.L1003:
	.align	2
.L1002:
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
.LVL829:
	.loc 1 207 0
	b	memcpy
.LVL830:
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
.LCFI128:
	.loc 4 91 0
	movs	r5, #0
.LVL831:
	.loc 4 87 0
	ldr	r4, .L1014
	.loc 4 89 0
	movs	r1, #0
	.loc 4 87 0
	ldr	r0, [r4, #3240]
	add	r6, r4, #152
	.loc 4 88 0
	ldr	r3, [r4, #3244]
	.loc 4 89 0
	ldrh	r2, [r4, #86]
	.loc 4 87 0
	str	r0, [r4, #3228]
	.loc 4 88 0
	str	r3, [r4, #3232]
	.loc 4 89 0
	bl	ftl_memset
.LVL832:
.L1006:
	.loc 4 91 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	blt	.L1007
	.loc 4 94 0
	ldr	r6, [r4, #3232]
.LVL833:
.LBB443:
.LBB444:
.LBB445:
	.loc 1 202 0
	movs	r2, #16
	movs	r1, #255
.LBE445:
.LBE444:
.LBE443:
	.loc 4 108 0
	ldr	r9, .L1014+12
	.loc 4 109 0
	ldr	r8, .L1014+4
	.loc 4 84 0
	movs	r5, #0
.LVL834:
.LBB450:
.LBB448:
.LBB446:
	.loc 1 202 0
	mov	r0, r6
.LBE446:
.LBE448:
.LBE450:
	.loc 4 129 0
	ldr	r10, .L1014+16
.LBB451:
.LBB449:
.LBB447:
	.loc 1 202 0
	bl	memset
.LVL835:
.LBE447:
.LBE449:
.LBE451:
	.loc 4 97 0
	movw	r3, #61649
	.loc 4 82 0
	mov	r7, r5
.LVL836:
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
.L1008:
	.loc 4 104 0
	ldr	r3, [r4, #3240]
	.loc 4 106 0
	mov	fp, #0
	.loc 4 107 0
	ldrh	r1, [r4, #128]
	ldrh	r2, [r4, #130]
	.loc 4 104 0
	str	r3, [r4, #3228]
	.loc 4 105 0
	ldr	r3, [r4, #3244]
	.loc 4 106 0
	str	fp, [r4, #3220]
	.loc 4 105 0
	str	r3, [r4, #3232]
	.loc 4 107 0
	orr	r3, r2, r1, lsl #10
	.loc 4 108 0
	ldrh	r0, [r6, #10]
	.loc 4 107 0
	str	r3, [r4, #3224]
	.loc 4 108 0
	ldrh	r3, [r4, #132]
	str	r0, [sp]
	mov	r0, r9
	bl	printk
.LVL837:
	.loc 4 109 0
	movs	r3, #1
	ldr	r0, .L1014+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL838:
	.loc 4 110 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r4, #130]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1009
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
	str	r3, [r4, #3224]
	.loc 4 121 0
	str	r3, [r0, #4]
	.loc 4 122 0
	bl	FlashEraseBlocks
.LVL839:
	.loc 4 123 0
	movs	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL840:
.L1009:
	.loc 4 126 0
	ldrh	r3, [r4, #130]
	adds	r3, r3, #1
	strh	r3, [r4, #130]	@ movhi
	.loc 4 127 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	bne	.L1010
	.loc 4 130 0
	adds	r5, r5, #1
.LVL841:
	.loc 4 129 0
	ldr	r1, [r4, #3224]
	.loc 4 130 0
	uxth	r5, r5
.LVL842:
	.loc 4 129 0
	mov	r0, r10
	bl	printk
.LVL843:
	.loc 4 131 0
	cmp	r5, #3
	bls	.L1008
	.loc 4 132 0
	mov	r2, r5
	ldr	r1, [r4, #3224]
	ldr	r0, .L1014+8
	bl	printk
.LVL844:
.L1012:
	b	.L1012
.LVL845:
.L1007:
	.loc 4 92 0 discriminator 3
	ldrh	r2, [r4, #3860]
	ldr	r3, [r4, #3228]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	.loc 4 91 0 discriminator 3
	adds	r5, r5, #1
	.loc 4 92 0 discriminator 3
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
.LVL846:
	b	.L1006
.LVL847:
.L1013:
	movs	r7, #1
	b	.L1008
.L1010:
	.loc 4 139 0
	cmp	r7, #0
	beq	.L1013
	.loc 4 146 0
	movs	r0, #0
	add	sp, sp, #12
.LCFI129:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL848:
.L1015:
	.align	2
.L1014:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
	.word	.LC95
	.word	.LC93
	.word	.LC94
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
	.loc 2 2579 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL849:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI130:
	mov	r5, r0
	ldr	r4, .L1051
	.pad #20
	sub	sp, sp, #20
.LCFI131:
	.loc 2 2585 0
	ldr	r9, .L1051+20
.LVL850:
.L1017:
	ldr	r3, .L1051
	ldr	r2, .L1051
	ldrh	r3, [r3, #2320]
	ldrh	r2, [r2, #2312]
	add	r3, r3, r2
	ldr	r2, .L1051
	ldrh	r2, [r2, #16]
	cmp	r3, r2
	ble	.L1018
	.loc 2 2585 0 is_stmt 0 discriminator 1
	movw	r2, #2585
	mov	r1, r9
	ldr	r0, .L1051+4
	bl	printk
.LVL851:
.L1018:
	.loc 2 2587 0 is_stmt 1
	ldr	r3, .L1051+8
	cmp	r5, r3
	bne	.L1043
	.loc 2 2588 0
	ldrh	r3, [r4, #2320]
	ldr	r1, [r4, #3136]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
.LVL852:
	.loc 2 2592 0
	cbz	r1, .L1019
	.loc 2 2593 0
	subs	r1, r1, #1
.LVL853:
	uxth	r1, r1
.LVL854:
.L1019:
	.loc 2 2594 0
	ldr	r0, .L1051+12
	bl	List_pop_index_node
.LVL855:
	ldrh	r3, [r4, #2320]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L1020
	.loc 2 2594 0 is_stmt 0 discriminator 1
	movw	r2, #2594
	mov	r1, r9
	ldr	r0, .L1051+4
	bl	printk
.LVL856:
.L1020:
	.loc 2 2594 0 discriminator 3
	ldrh	r3, [r4, #2320]
	.loc 2 2598 0 is_stmt 1 discriminator 3
	mov	r0, r5
	.loc 2 2594 0 discriminator 3
	subs	r3, r3, #1
	strh	r3, [r4, #2320]	@ movhi
	.loc 2 2597 0 discriminator 3
	strh	r8, [r5]	@ movhi
	.loc 2 2598 0 discriminator 3
	bl	make_superblock
.LVL857:
	.loc 2 2599 0 discriminator 3
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1021
	.loc 2 2603 0
	ldr	r3, [r4, #2300]
	uxth	r6, r6
	movw	r2, #65535
	.loc 2 2604 0
	mov	r0, r8
	.loc 2 2603 0
	strh	r2, [r3, r6, lsl #1]	@ movhi
	.loc 2 2604 0
	bl	INSERT_DATA_LIST
.LVL858:
	.loc 2 2605 0
	ldrh	r2, [r4, #2312]
	ldrh	r3, [r4, #2320]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1017
	.loc 2 2605 0 is_stmt 0 discriminator 1
	movw	r2, #2605
	mov	r1, r9
	ldr	r0, .L1051+4
	bl	printk
.LVL859:
	b	.L1017
.L1043:
	movs	r1, #0
.LVL860:
	b	.L1019
.LVL861:
.L1021:
	.loc 2 2608 0 is_stmt 1
	ldrh	r2, [r4, #2312]
	ldrh	r3, [r4, #2320]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1023
	.loc 2 2608 0 is_stmt 0 discriminator 1
	mov	r2, #2608
	mov	r1, r9
	ldr	r0, .L1051+4
	bl	printk
.LVL862:
.L1023:
	.loc 2 2612 0 is_stmt 1 discriminator 1
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
.L1024:
	.loc 2 2611 0 discriminator 1
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L1026
	.loc 2 2619 0
	cbnz	r7, .L1027
	.loc 2 2619 0 is_stmt 0 discriminator 1
	movw	r2, #2619
	mov	r1, r9
	ldr	r0, .L1051+4
	bl	printk
.LVL863:
.L1027:
	.loc 2 2621 0 is_stmt 1
	ldrh	r3, [r4, #2564]
	cmp	r3, r8
	bne	.L1028
	.loc 2 2621 0 is_stmt 0 discriminator 1
	movw	r2, #2621
	mov	r1, r9
	ldr	r0, .L1051+4
	bl	printk
.LVL864:
.L1028:
	.loc 2 2622 0 is_stmt 1
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	.loc 2 2623 0
	ldr	r2, [r4, #2304]
	.loc 2 2622 0
	cmp	r3, #0
	bne	.L1029
	.loc 2 2623 0
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L1030
	.loc 2 2624 0
	ldrh	r1, [r4, #68]
	add	r3, r3, r1
.L1050:
	.loc 2 2626 0
	strh	r3, [r2, r6, lsl #1]	@ movhi
	.loc 2 2628 0
	movs	r1, #0
	.loc 2 2627 0
	ldr	r3, [r4, #2524]
	.loc 2 2628 0
	mov	r0, r8
	.loc 2 2627 0
	adds	r3, r3, #1
	str	r3, [r4, #2524]
	.loc 2 2628 0
	bl	ftl_set_blk_mode
.LVL865:
.L1032:
	.loc 2 2635 0
	ldr	r3, [r4, #2304]
	ldr	r2, [r4, #2540]
	.loc 2 2638 0
	ldrh	r0, [r4, #68]
	.loc 2 2635 0
	ldrh	r3, [r3, r6, lsl #1]
	.loc 2 2638 0
	ldrh	r1, [r4, #16]
	.loc 2 2635 0
	cmp	r3, r2
	.loc 2 2638 0
	ldr	r2, [r4, #2524]
	.loc 2 2636 0
	it	hi
	strhi	r3, [r4, #2540]
	.loc 2 2638 0
	ldr	r3, [r4, #2528]
	mla	r0, r2, r0, r3
	bl	__aeabi_uidiv
.LVL866:
	.loc 2 2639 0
	ldr	r2, [r4, #3820]
	.loc 2 2642 0
	ldr	r1, [r4, #3760]
	.loc 2 2638 0
	str	r0, [r4, #2532]
	.loc 2 2639 0
	ldr	r3, [r2, #16]
	.loc 2 2642 0
	ldr	r0, .L1051+16
	.loc 2 2639 0
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L1034:
	adds	r3, r3, #20
	.loc 2 2641 0 discriminator 1
	cmp	r2, r3
	bne	.L1035
	.loc 2 2643 0
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3760]
	.loc 2 2645 0
	mov	r10, #0
	.loc 2 2643 0
	bl	FlashEraseBlocks
.LVL867:
	.loc 2 2644 0
	mov	r3, r10
.LVL868:
	.loc 2 2646 0
	movs	r1, #20
.L1036:
	.loc 2 2645 0 discriminator 1
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L1038
	.loc 2 2656 0
	cmp	r3, #0
	ble	.L1039
	.loc 2 2657 0
	mov	r0, r8
	bl	update_multiplier_value
.LVL869:
	.loc 2 2658 0
	bl	FtlBbmTblFlush
.LVL870:
.L1039:
	.loc 2 2661 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1040
	.loc 2 2663 0
	ldr	r3, [r4, #2300]
.LVL871:
	movw	r2, #65535
	.loc 2 2664 0
	mov	r0, r8
	.loc 2 2663 0
	strh	r2, [r3, r6, lsl #1]	@ movhi
	.loc 2 2664 0
	bl	INSERT_DATA_LIST
.LVL872:
	.loc 2 2665 0
	b	.L1017
.L1026:
	.loc 2 2612 0
	str	ip, [r3, #8]
	.loc 2 2614 0
	movw	lr, #65535
	.loc 2 2613 0
	str	ip, [r3, #12]
	.loc 2 2614 0
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L1025
	.loc 2 2615 0
	mla	lr, r10, r7, r1
	.loc 2 2616 0
	adds	r7, r7, #1
.LVL873:
	.loc 2 2615 0
	lsls	r2, r2, #10
	.loc 2 2616 0
	uxth	r7, r7
.LVL874:
	.loc 2 2615 0
	str	r2, [lr, #4]
.L1025:
	adds	r3, r3, #20
	b	.L1024
.L1030:
	.loc 2 2626 0
	movs	r3, #2
	b	.L1050
.L1029:
	.loc 2 2630 0
	ldrh	r3, [r2, r6, lsl #1]
.LBB452:
.LBB453:
	mov	r0, r8
.LBE453:
.LBE452:
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	.loc 2 2631 0
	ldr	r3, [r4, #2528]
	adds	r3, r3, #1
	str	r3, [r4, #2528]
.LBB455:
.LBB454:
	bl	ftl_set_blk_mode.part.6
.LVL875:
	b	.L1032
.L1035:
.LBE454:
.LBE455:
	.loc 2 2642 0 discriminator 3
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1034
.LVL876:
.L1038:
	.loc 2 2646 0
	mul	r2, r1, r10
	ldr	r0, [r4, #3760]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L1037
	.loc 2 2648 0
	ldr	r0, [ip, #4]
	.loc 2 2647 0
	adds	r3, r3, #1
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	.loc 2 2649 0
	ubfx	r0, r0, #10, #16
	.loc 2 2647 0
	str	r3, [sp, #4]
	.loc 2 2649 0
	bl	FtlBbmMapBadBlock
.LVL877:
	.loc 2 2650 0
	ldr	r2, [sp, #8]
	.loc 2 2651 0
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
.LVL878:
	.loc 2 2650 0
	strh	r2, [fp]	@ movhi
	.loc 2 2651 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	subs	r2, r2, #1
	strb	r2, [r5, #7]
.L1037:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L1036
.L1040:
	.loc 2 2668 0
	ldrh	r3, [r4, #78]
.LVL879:
	.loc 2 2669 0
	strh	r8, [r5]	@ movhi
	.loc 2 2668 0
	smulbb	r3, r3, r2
	.loc 2 2670 0
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	.loc 2 2671 0
	strb	r2, [r5, #6]
	.loc 2 2672 0
	ldr	r2, [r4, #2516]
	.loc 2 2668 0
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	.loc 2 2672 0
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #2516]
	.loc 2 2673 0
	ldr	r2, [r4, #2300]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 2 2674 0
	ldrh	r3, [r5, #4]
	cbz	r3, .L1041
	.loc 2 2674 0 is_stmt 0 discriminator 2
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1042
.L1041:
	.loc 2 2674 0 discriminator 3
	movw	r2, #2674
	ldr	r1, .L1051+20
	ldr	r0, .L1051+4
	bl	printk
.LVL880:
.L1042:
	.loc 2 2676 0 is_stmt 1
	movs	r0, #0
	add	sp, sp, #20
.LCFI132:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL881:
.L1052:
	.align	2
.L1051:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+2420
	.word	.LANCHOR0+2316
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
.LVL882:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI133:
	.loc 5 390 0
	mov	r8, r0
	.loc 5 395 0
	ldr	r4, .L1065
	ldrh	r3, [r4, #3182]
	cbz	r3, .L1054
	movs	r7, #0
	add	r10, r4, #3184
.LVL883:
.L1055:
.LBB458:
.LBB459:
	.loc 5 396 0
	ldrh	r2, [r4, #8]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1061
	.loc 5 412 0
	bl	FtlGcReFreshBadBlk
.LVL884:
.L1054:
.LBE459:
.LBE458:
	.loc 5 415 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL885:
.L1061:
.LBB461:
.LBB460:
	.loc 5 397 0
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL886:
	mov	r9, r0
.L1056:
	.loc 5 398 0
	ldrh	r3, [r4, #3182]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L1060
	adds	r7, r7, #1
	b	.L1055
.L1060:
	.loc 5 399 0
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3184]
	cmp	r3, r9
	bne	.L1057
	.loc 5 401 0
	mov	r1, r9
	ldr	r0, .L1065+4
	bl	printk
.LVL887:
	.loc 5 403 0
	mov	r0, r9
	bl	FtlBbmMapBadBlock
.LVL888:
	.loc 5 404 0
	bl	FtlBbmTblFlush
.LVL889:
	.loc 5 405 0
	ldrh	r1, [r4, #3182]
	add	r3, r10, r6, lsl #1
.L1058:
	cmp	r5, r1
	bcc	.L1059
	.loc 5 407 0
	subs	r1, r1, #1
	strh	r1, [r4, #3182]	@ movhi
.L1057:
	add	fp, fp, #1
	b	.L1056
.L1059:
	.loc 5 406 0
	ldrh	r0, [r3, #2]!
	.loc 5 405 0
	adds	r5, r5, #1
.LVL890:
	uxth	r5, r5
.LVL891:
	.loc 5 406 0
	strh	r0, [r3, #-2]	@ movhi
	b	.L1058
.L1066:
	.align	2
.L1065:
	.word	.LANCHOR0
	.word	.LC96
.LBE460:
.LBE461:
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
	.loc 2 2735 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL892:
	.loc 2 2736 0
	ldr	r2, .L1077
	.loc 2 2735 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI134:
	.loc 2 2735 0
	mov	r5, r0
	mov	r4, r2
	.loc 2 2736 0
	ldr	r3, [r2, #2300]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L1068
	.loc 2 2737 0
	ldrh	r1, [r2, #2564]
	cmp	r1, r0
	bne	.L1069
	.loc 2 2739 0
	movw	r3, #65535
	strh	r3, [r2, #2564]	@ movhi
.L1070:
.LVL893:
.LBB464:
.LBB465:
	.loc 2 2744 0
	mov	r1, r5
	ldr	r0, .L1077+4
	bl	List_remove_node
.LVL894:
	ldrh	r3, [r4, #2312]
	cbnz	r3, .L1072
	movw	r2, #2744
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	printk
.LVL895:
.L1072:
	ldrh	r3, [r4, #2312]
	.loc 2 2745 0
	mov	r0, r5
	.loc 2 2744 0
	subs	r3, r3, #1
	strh	r3, [r4, #2312]	@ movhi
	.loc 2 2745 0
	bl	free_data_superblock
.LVL896:
	.loc 2 2746 0
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
.LVL897:
	.loc 2 2747 0
	ldrh	r2, [r4, #2312]
	ldrh	r3, [r4, #2320]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1076
	movw	r2, #2747
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	printk
.LVL898:
.L1076:
	movs	r3, #1
	b	.L1067
.LVL899:
.L1069:
.LBE465:
.LBE464:
	.loc 2 2740 0
	ldrh	r1, [r2, #2324]
	cmp	r1, r0
	beq	.L1067
	.loc 2 2740 0 is_stmt 0 discriminator 1
	ldrh	r1, [r2, #2372]
	cmp	r1, r0
	beq	.L1067
	.loc 2 2740 0 discriminator 2
	ldrh	r2, [r2, #2420]
	cmp	r2, r0
	bne	.L1070
.L1067:
	.loc 2 2753 0 is_stmt 1
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.LVL900:
.L1068:
	.loc 2 2750 0
	bl	List_update_data_list
.LVL901:
	.loc 2 2752 0
	movs	r3, #0
	b	.L1067
.L1078:
	.align	2
.L1077:
	.word	.LANCHOR0
	.word	.LANCHOR0+2296
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
	.loc 2 2756 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL902:
	.loc 2 2759 0
	movw	r3, #65535
	.loc 2 2756 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI135:
	.loc 2 2759 0
	cmp	r0, r3
	.loc 2 2756 0
	mov	r4, r0
	ldr	r5, .L1088
	.loc 2 2759 0
	beq	.L1080
	.loc 2 2760 0
	ldr	r3, [r5, #2300]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1081
	.loc 2 2761 0
	mov	r1, r0
	ldr	r0, .L1088+4
	bl	printk
.LVL903:
	.loc 2 2762 0
	ldr	r3, [r5, #2300]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L1082
.L1086:
	.loc 2 2771 0
	movs	r6, #0
.L1079:
	.loc 2 2781 0
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.LVL904:
.L1082:
	.loc 2 2762 0 discriminator 1
	movw	r2, #2762
.L1087:
	.loc 2 2778 0 discriminator 2
	ldr	r1, .L1088+8
	ldr	r0, .L1088+12
	bl	printk
.LVL905:
	b	.L1079
.LVL906:
.L1081:
	.loc 2 2765 0
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L1080:
	.loc 2 2769 0
	ldrh	r0, [r5, #3952]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1084
	.loc 2 2770 0
	strh	r4, [r5, #3952]	@ movhi
	b	.L1086
.L1084:
	.loc 2 2772 0
	cmp	r4, r0
	beq	.L1086
	.loc 2 2775 0
	bl	update_vpc_list
.LVL907:
	.loc 2 2778 0
	ldr	r2, [r5, #2292]
	.loc 2 2775 0
	adds	r6, r0, #0
	.loc 2 2778 0
	ldr	r3, [r5, #2296]
	.loc 2 2775 0
	it	ne
	movne	r6, #1
	.loc 2 2777 0
	strh	r4, [r5, #3952]	@ movhi
	.loc 2 2778 0
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L1088+16
	muls	r3, r2, r3
	ldr	r2, [r5, #2300]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L1079
	.loc 2 2778 0 is_stmt 0 discriminator 1
	cmp	r4, r1
	beq	.L1079
	.loc 2 2778 0 discriminator 2
	movw	r2, #2778
	b	.L1087
.L1089:
	.align	2
.L1088:
	.word	.LANCHOR0
	.word	.LC97
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
	.loc 2 996 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI136:
	.pad #24
	sub	sp, sp, #24
.LCFI137:
	.loc 2 997 0
	ldr	r4, .L1108
	ldrh	r2, [r4, #2328]
	cmp	r2, #0
	beq	.L1091
	.loc 2 997 0 is_stmt 0 discriminator 1
	ldrb	r3, [r4, #2332]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1091
	.loc 2 998 0 is_stmt 1
	ldrb	r1, [r4, #2331]	@ zero_extendqisi2
	ldrh	r3, [r4, #78]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1091
.LBB469:
.LBB470:
	.loc 2 1005 0
	ldrb	r7, [r4, #2334]	@ zero_extendqisi2
	cbnz	r7, .L1090
	.loc 2 1000 0
	ldr	r6, [r4, #2480]
	.loc 2 1007 0
	mov	r2, r7
	mov	r1, sp
	.loc 2 1002 0
	ldrh	r8, [r4, #8]
	.loc 2 1000 0
	subs	r6, r6, #1
.LVL908:
	.loc 2 1007 0
	mov	r0, r6
	bl	log2phys
.LVL909:
	.loc 2 1010 0
	ldr	r3, [sp]
	.loc 2 1013 0
	ldr	r5, [r4, #3244]
	.loc 2 1012 0
	ldr	r0, [r4, #3240]
	.loc 2 1010 0
	str	r3, [sp, #8]
	.loc 2 1016 0
	adds	r3, r3, #1
	.loc 2 1011 0
	str	r6, [sp, #20]
	.loc 2 1012 0
	str	r0, [sp, #12]
	.loc 2 1013 0
	str	r5, [sp, #16]
	.loc 2 1015 0
	str	r7, [r5, #4]
	.loc 2 1016 0
	beq	.L1093
	.loc 2 1018 0
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.LVL910:
.L1094:
	.loc 2 1035 0
	ldr	r9, .L1108+4
	.loc 2 1024 0
	movs	r7, #0
	.loc 2 1002 0
	lsl	r8, r8, #2
.LVL911:
	.loc 2 1036 0
	mov	r10, r7
	.loc 2 1024 0
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1095:
	.loc 2 1025 0
	cmp	r8, r7
	bne	.L1099
.L1096:
	.loc 2 1041 0
	movs	r3, #1
.L1107:
.LBE470:
.LBE469:
	.loc 2 1047 0
	strb	r3, [r4, #2334]
.L1090:
	.loc 2 1049 0
	add	sp, sp, #24
.LCFI138:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL912:
.L1093:
.LCFI139:
.LBB472:
.LBB471:
	.loc 2 1022 0
	ldrh	r2, [r4, #86]
	movs	r1, #255
	bl	ftl_memset
.LVL913:
	b	.L1094
.LVL914:
.L1099:
	.loc 2 1027 0
	ldrh	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L1096
	.loc 2 1032 0
	ldr	r3, [sp, #8]
	.loc 2 1035 0
	mov	r0, r9
	.loc 2 1031 0
	str	r6, [r5, #8]
	adds	r7, r7, #1
	.loc 2 1032 0
	str	r3, [r5, #12]
	.loc 2 1033 0
	ldrh	r3, [r4, #2324]
	strh	r3, [r5, #2]	@ movhi
	.loc 2 1035 0
	bl	get_new_active_ppa
.LVL915:
	.loc 2 1036 0
	ldr	r3, [r4, #2520]
	.loc 2 1037 0
	movs	r1, #1
	.loc 2 1035 0
	str	r0, [sp, #8]
	.loc 2 1037 0
	add	r0, sp, #4
	.loc 2 1036 0
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r10
	str	r3, [r4, #2520]
	.loc 2 1037 0
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL916:
	.loc 2 1039 0
	ldrh	r0, [r4, #2324]
	bl	decrement_vpc_count
.LVL917:
	b	.L1095
.LVL918:
.L1091:
.LBE471:
.LBE472:
	.loc 2 1047 0
	movs	r3, #0
	b	.L1107
.L1109:
	.align	2
.L1108:
	.word	.LANCHOR0
	.word	.LANCHOR0+2324
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
	.loc 2 789 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI140:
	.loc 2 795 0
	movs	r4, #0
	ldr	r5, .L1114
	.loc 2 796 0
	movs	r6, #12
	.loc 2 793 0
	bl	FtlWriteDump_data
.LVL919:
.L1111:
	.loc 2 795 0 discriminator 1
	ldrh	r3, [r5, #114]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1113
	.loc 2 799 0
	movs	r0, #0
.LVL920:
	pop	{r4, r5, r6, pc}
.LVL921:
.L1113:
	.loc 2 796 0
	ldr	r2, [r5, #2468]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1112
	.loc 2 797 0
	bl	flush_l2p_region
.LVL922:
.L1112:
	adds	r4, r4, #1
	b	.L1111
.L1115:
	.align	2
.L1114:
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
	.loc 2 1705 0
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL923:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI141:
	.loc 2 1719 0
	movw	r2, #65535
	ldrh	r3, [r0]
	.loc 2 1705 0
	.pad #52
	sub	sp, sp, #52
.LCFI142:
	.loc 2 1705 0
	mov	fp, r0
	.loc 2 1719 0
	cmp	r3, r2
	beq	.L1241
	.loc 2 1724 0
	ldrh	r3, [r0, #2]
.LVL924:
	.loc 2 1730 0
	ldr	r4, .L1248
	.loc 2 1724 0
	str	r3, [sp, #8]
	.loc 2 1725 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
.LVL925:
	.loc 2 1730 0
	ldr	r1, [sp, #8]
.LVL926:
	.loc 2 1725 0
	str	r3, [sp, #24]
	.loc 2 1730 0
	ldrh	r3, [r4, #78]
.LVL927:
	cmp	r3, r1
	mov	r3, #0
	bne	.L1119
	.loc 2 1731 0
	strh	r3, [r0, #4]	@ movhi
.LVL928:
.L1246:
	.loc 2 2038 0
	strb	r3, [fp, #6]
.L1241:
	.loc 2 2059 0
	movs	r0, #0
	add	sp, sp, #52
.LCFI143:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL929:
.L1119:
.LCFI144:
	.loc 2 1738 0
	ldrh	r0, [r0, #16]
.LVL930:
.L1120:
	.loc 2 1739 0
	cmp	r0, r2
	uxth	r5, r3
	add	r3, r3, #1
	beq	.L1121
	.loc 2 1746 0
	movs	r1, #1
.LVL931:
	bl	FtlGetLastWrittenPage
.LVL932:
	.loc 2 1754 0
	adds	r7, r0, #1
	.loc 2 1746 0
	mov	r6, r0
.LVL933:
	.loc 2 1754 0
	beq	.L1122
	.loc 2 1770 0
	ldr	r3, [r4, #3148]
	.loc 2 1771 0
	movs	r2, #0
	.loc 2 1766 0
	ldrh	lr, [r4, #8]
	.loc 2 1768 0
	movw	r9, #65535
	.loc 2 1769 0
	ldr	r0, [r4, #3792]
	mov	r10, #20
	.loc 2 1770 0
	str	r3, [sp, #4]
	ldrh	r3, [r4, #86]
	.loc 2 1771 0
	ldr	r8, [r4, #3152]
	ldrh	r7, [r4, #88]
	mov	r4, r2
	.loc 2 1770 0
	str	r3, [sp, #12]
	add	r3, fp, #16
	.loc 2 1771 0
	mov	ip, r3
	str	r3, [sp, #20]
.L1123:
	.loc 2 1766 0 discriminator 1
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L1129
	.loc 2 1777 0
	ldr	r5, .L1248
	.loc 2 1776 0
	movs	r2, #0
	mov	r1, r4
	.loc 2 1715 0
	movw	r9, #65535
	.loc 2 1776 0
	bl	FlashReadPages
.LVL934:
	.loc 2 1777 0
	ldr	r3, [r5, #2520]
	.loc 2 1791 0
	uxth	r2, r6
	.loc 2 1779 0
	ldr	r8, [r5, #3792]
	.loc 2 1791 0
	str	r2, [sp, #12]
	.loc 2 1777 0
	subs	r3, r3, #1
.LVL935:
	str	r3, [sp, #4]
	mov	r7, r8
	.loc 2 1778 0
	movs	r3, #0
.LVL936:
.L1130:
	uxth	r2, r3
.LVL937:
	.loc 2 1778 0 is_stmt 0 discriminator 1
	cmp	r4, r2
	bhi	.L1135
	.loc 2 1794 0 is_stmt 1
	bne	.L1133
	.loc 2 1796 0
	adds	r3, r6, #1
	uxth	r3, r3
.LVL938:
	str	r3, [sp, #16]
.L1242:
	.loc 2 1802 0
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
.LVL939:
	.loc 2 1807 0
	ldrh	r3, [r5, #78]
	ldr	r2, [sp, #16]
.LVL940:
	.loc 2 1802 0
	str	r0, [sp, #12]
	.loc 2 1807 0
	cmp	r3, r2
	bne	.L1137
	.loc 2 1808 0
	ldrh	r3, [sp, #16]
	strh	r3, [fp, #2]	@ movhi
	.loc 2 1809 0
	movs	r3, #0
	strb	r3, [fp, #6]
	.loc 2 1810 0
	strh	r3, [fp, #4]	@ movhi
.L1137:
	.loc 2 1813 0
	ldr	r3, [sp, #16]
.LVL941:
	ldr	r2, [sp, #8]
.LVL942:
	cmp	r3, r2
	bne	.L1138
	.loc 2 1813 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #12]
.LVL943:
	ldr	r2, [sp, #24]
.LVL944:
	cmp	r3, r2
	bne	.L1138
	.loc 2 1814 0 is_stmt 1
	ldr	r1, [sp, #16]
	mov	r2, r3
.LVL945:
.L1247:
	.loc 2 2053 0
	mov	r0, fp
.LVL946:
	bl	ftl_sb_update_avl_pages
.LVL947:
	b	.L1241
.LVL948:
.L1121:
	.loc 2 1741 0
	uxth	r1, r3
.LVL949:
	adds	r1, r1, #8
	ldrh	r0, [fp, r1, lsl #1]
	b	.L1120
.LVL950:
.L1122:
	.loc 2 1755 0
	ldr	r3, [sp, #8]
.LVL951:
	cbz	r3, .L1124
	.loc 2 1755 0 is_stmt 0 discriminator 1
	movw	r2, #1755
	ldr	r1, .L1248+4
	ldr	r0, .L1248+8
	bl	printk
.LVL952:
.L1124:
	.loc 2 1756 0 is_stmt 1
	ldr	r3, [sp, #24]
.LVL953:
	cbz	r3, .L1125
	.loc 2 1756 0 is_stmt 0 discriminator 1
	cmp	r5, r3
	beq	.L1125
	.loc 2 1756 0 discriminator 2
	movw	r2, #1756
	ldr	r1, .L1248+4
	ldr	r0, .L1248+8
	bl	printk
.LVL954:
.L1125:
	.loc 2 1757 0 is_stmt 1
	movs	r3, #0
.LVL955:
	strh	r3, [fp, #2]	@ movhi
	b	.L1246
.L1129:
	.loc 2 1767 0
	ldrh	r3, [ip], #2
	.loc 2 1768 0
	cmp	r3, r9
	beq	.L1126
	.loc 2 1769 0
	mla	r1, r10, r4, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	.loc 2 1770 0
	ldr	r3, [sp, #12]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r5, r3
	ldr	r3, [sp, #4]
	add	r5, r5, r3
	.loc 2 1771 0
	mov	r3, r7
	muls	r3, r4, r3
	.loc 2 1772 0
	add	r4, r4, #1
	.loc 2 1771 0
	it	mi
	addmi	r3, r3, #3
	.loc 2 1772 0
	uxth	r4, r4
	.loc 2 1771 0
	bic	r3, r3, #3
	.loc 2 1770 0
	str	r5, [r1, #8]
	.loc 2 1771 0
	add	r3, r3, r8
	str	r3, [r1, #12]
.L1126:
	adds	r2, r2, #1
	b	.L1123
.LVL956:
.L1135:
	.loc 2 1779 0
	ldr	r2, [r7]
.LVL957:
	cbnz	r2, .L1131
	.loc 2 1780 0
	ldr	r10, [r7, #12]
	.loc 2 1781 0
	ldr	r2, [r10, #4]
	adds	r0, r2, #1
	beq	.L1132
	.loc 2 1782 0
	ldr	r1, [r5, #2520]
	mov	r0, r2
	bl	ftl_cmp_data_ver
.LVL958:
	cbz	r0, .L1132
	.loc 2 1784 0
	adds	r2, r2, #1
	str	r2, [r5, #2520]
.L1132:
	.loc 2 1787 0
	ldr	r2, [r10]
	adds	r2, r2, #1
	bne	.L1134
.LVL959:
.L1133:
	.loc 2 1801 0
	uxth	r2, r6
.LVL960:
	.loc 2 1802 0
	uxth	r3, r3
	.loc 2 1801 0
	str	r2, [sp, #16]
	.loc 2 1802 0
	movs	r2, #20
.LVL961:
	mla	r8, r2, r3, r8
	b	.L1242
.L1131:
	.loc 2 1791 0
	ldr	r9, [sp, #12]
.L1134:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1130
.LVL962:
.L1138:
	.loc 2 1819 0
	movw	r3, #65535
.LVL963:
	cmp	r9, r3
	bne	.L1139
	.loc 2 1819 0 is_stmt 0 discriminator 1
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1140
.L1139:
.LBB473:
	.loc 2 1824 0 is_stmt 1
	ldr	r3, [r5, #3964]
	.loc 2 1823 0
	uxth	r8, r6
	.loc 2 1827 0
	uxth	r6, r6
.LVL964:
	ldr	r7, .L1248
	.loc 2 1833 0
	movw	r9, #65535
	.loc 2 1824 0
	adds	r3, r3, #1
	.loc 2 1825 0
	itt	eq
	ldreq	r3, [sp, #4]
.LVL965:
	streq	r3, [r5, #3964]
	.loc 2 1827 0
	ldr	r3, [sp, #8]
.LVL966:
	adds	r3, r3, #7
.LVL967:
	cmp	r6, r3
	.loc 2 1828 0
	itet	gt
	subgt	r4, r8, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L1143:
	.loc 2 1829 0 discriminator 1
	cmp	r4, r8
	bhi	.L1150
	.loc 2 1834 0
	movs	r3, #0
	.loc 2 1831 0
	ldrh	lr, [r7, #8]
	.loc 2 1834 0
	ldr	r0, [r7, #3792]
	mov	r6, r3
	ldr	r1, [sp, #20]
	mov	ip, #20
	b	.L1151
.LVL968:
.L1145:
	.loc 2 1832 0
	ldrh	r2, [r1], #2
	.loc 2 1833 0
	cmp	r2, r9
	beq	.L1144
	.loc 2 1834 0
	mla	r10, ip, r6, r0
	.loc 2 1835 0
	adds	r6, r6, #1
	.loc 2 1834 0
	orr	r2, r4, r2, lsl #10
	.loc 2 1835 0
	uxth	r6, r6
	.loc 2 1834 0
	str	r2, [r10, #4]
.L1144:
	adds	r3, r3, #1
.LVL969:
.L1151:
	.loc 2 1831 0 discriminator 1
	uxth	r2, r3
.LVL970:
	cmp	r2, lr
	bcc	.L1145
	.loc 2 1838 0
	movs	r2, #0
	mov	r1, r6
	bl	FlashReadPages
.LVL971:
	.loc 2 1840 0
	ldr	r3, [r7, #3792]
	movs	r2, #20
	mla	r6, r2, r6, r3
.L1146:
	.loc 2 1839 0 discriminator 1
	cmp	r6, r3
	bne	.L1149
	.loc 2 1829 0
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1143
.L1149:
	.loc 2 1840 0
	ldr	r2, [r3]
	cbnz	r2, .L1140
	.loc 2 1841 0
	ldr	r2, [r3, #12]
	.loc 2 1842 0
	ldrh	r1, [r2]
	cmp	r1, r9
	beq	.L1148
	.loc 2 1844 0
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	.loc 2 1846 0
	it	ne
	strne	r2, [r7, #3964]
.L1148:
	adds	r3, r3, #20
	b	.L1146
.LVL972:
.L1150:
	.loc 2 1858 0
	mov	r3, #-1
	str	r3, [r7, #3964]
.LVL973:
.L1140:
.LBE473:
	.loc 2 1863 0
	movs	r3, #1
	.loc 2 1864 0
	ldr	r0, .L1248+12
	.loc 2 1863 0
	strh	r3, [r5, #3968]	@ movhi
	.loc 2 1870 0
	ldr	r4, .L1248
	.loc 2 1864 0
	bl	FtlMapBlkWriteDump_data
.LVL974:
	ldr	r5, [sp, #8]
.LVL975:
.L1152:
	.loc 2 1870 0
	ldrh	r6, [r4, #8]
.LVL976:
	movs	r3, #0
	.loc 2 1873 0
	ldr	r0, [r4, #3792]
.LVL977:
	.loc 2 1869 0
	mov	r10, r3
	.loc 2 1870 0
	ldr	r1, [sp, #20]
	.loc 2 1872 0
	movw	r7, #65535
	.loc 2 1873 0
	mov	ip, #20
.L1153:
	.loc 2 1870 0 discriminator 1
	uxth	r2, r3
.LVL978:
	cmp	r6, r2
	bhi	.L1155
	.loc 2 1877 0
	movs	r2, #0
	mov	r1, r10
	bl	FlashReadPages
.LVL979:
	.loc 2 1878 0
	movs	r3, #0
.LVL980:
.L1245:
	str	r3, [sp, #28]
	.loc 2 1878 0 is_stmt 0 discriminator 2
	ldrh	r3, [sp, #28]
	cmp	r10, r3
	bhi	.L1185
	.loc 2 2031 0 is_stmt 1
	adds	r5, r5, #1
	.loc 2 2032 0
	ldrh	r3, [r4, #78]
	.loc 2 2031 0
	uxth	r5, r5
	.loc 2 2032 0
	cmp	r3, r5
	bne	.L1152
	.loc 2 2035 0
	ldrh	r2, [r4, #8]
.LVL981:
	.loc 2 2037 0
	movw	r0, #65535
.LVL982:
	.loc 2 2034 0
	movs	r3, #0
	.loc 2 2033 0
	strh	r5, [fp, #2]	@ movhi
	.loc 2 2034 0
	strh	r3, [fp, #4]	@ movhi
.L1186:
	.loc 2 2035 0 discriminator 1
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1241
	.loc 2 2036 0
	ldr	r1, [sp, #20]
	ldrh	r4, [r1], #2
	.loc 2 2037 0
	cmp	r4, r0
	.loc 2 2036 0
	str	r1, [sp, #20]
	add	r1, r3, #1
	.loc 2 2037 0
	bne	.L1246
	mov	r3, r1
	b	.L1186
.LVL983:
.L1155:
	.loc 2 1871 0
	ldrh	r2, [r1], #2
	.loc 2 1872 0
	cmp	r2, r7
	beq	.L1154
	.loc 2 1873 0
	mla	lr, ip, r10, r0
	.loc 2 1874 0
	add	r10, r10, #1
	.loc 2 1873 0
	orr	r2, r5, r2, lsl #10
	.loc 2 1874 0
	uxth	r10, r10
	.loc 2 1873 0
	str	r2, [lr, #4]
.L1154:
	adds	r3, r3, #1
	b	.L1153
.L1249:
	.align	2
.L1248:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3864
.LVL984:
.L1185:
	ldr	r3, [sp, #28]
	movs	r7, #20
	.loc 2 1879 0
	ldr	r9, [r4, #3792]
.LVL985:
	muls	r7, r3, r7
	add	r8, r9, r7
	ldr	r6, [r8, #4]
.LVL986:
	.loc 2 1880 0
	ubfx	r0, r6, #10, #16
.LVL987:
	.loc 2 1879 0
	str	r6, [sp, #44]
	.loc 2 1880 0
	bl	P2V_plane
.LVL988:
	.loc 2 1882 0
	ldr	r3, [sp, #8]
.LVL989:
	cmp	r5, r3
	bcc	.L1157
	.loc 2 1882 0 is_stmt 0 discriminator 1
	bne	.L1158
	.loc 2 1883 0 is_stmt 1
	ldr	r3, [sp, #24]
.LVL990:
	cmp	r3, r0
	bhi	.L1157
.LVL991:
.L1158:
	.loc 2 1886 0
	ldr	r3, [sp, #16]
.LVL992:
	cmp	r5, r3
	bne	.L1159
	.loc 2 1886 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #12]
.LVL993:
	cmp	r3, r0
	beq	.L1160
.LVL994:
.L1159:
	.loc 2 1890 0 is_stmt 1
	ldr	r3, [r9, r7]
.LVL995:
	adds	r3, r3, #1
	beq	.L1161
	.loc 2 1891 0
	ldr	r8, [r8, #12]
	.loc 2 1892 0
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1162
.LVL996:
.L1169:
.LBB474:
	.loc 2 1925 0
	ldrh	r0, [fp]
.LVL997:
.L1244:
.LBE474:
	.loc 2 2009 0
	bl	decrement_vpc_count
.LVL998:
.L1157:
	ldr	r3, [sp, #28]
.LVL999:
	adds	r3, r3, #1
	b	.L1245
.LVL1000:
.L1162:
	.loc 2 1898 0
	ldr	r3, [r8, #4]
.LVL1001:
	.loc 2 1899 0
	adds	r6, r3, #1
	.loc 2 1898 0
	str	r3, [sp, #4]
	.loc 2 1899 0
	beq	.L1163
	.loc 2 1900 0 discriminator 1
	ldr	r1, [r4, #2520]
	mov	r0, r3
.LVL1002:
	bl	ftl_cmp_data_ver
.LVL1003:
	.loc 2 1899 0 discriminator 1
	cbz	r0, .L1163
	.loc 2 1902 0
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
.LVL1004:
	str	r3, [r4, #2520]
.LVL1005:
.L1163:
	.loc 2 1905 0
	ldrh	r2, [r8]
	movw	r3, #61589
.LVL1006:
	cmp	r2, r3
	beq	.L1164
	.loc 2 1905 0 is_stmt 0 discriminator 1
	movw	r2, #1905
	ldr	r1, .L1250
	ldr	r0, .L1250+4
.LVL1007:
	bl	printk
.LVL1008:
.L1164:
	.loc 2 1906 0 is_stmt 1
	ldr	r6, [r8, #8]
.LVL1009:
	.loc 2 1908 0
	add	r1, sp, #40
	.loc 2 1907 0
	ldr	r3, [r8, #12]
	.loc 2 1908 0
	movs	r2, #0
	mov	r0, r6
.LVL1010:
	.loc 2 1907 0
	str	r3, [sp, #36]
	.loc 2 1908 0
	bl	log2phys
.LVL1011:
	.loc 2 1909 0
	ldr	r1, [r4, #3964]
	adds	r0, r1, #1
	beq	.L1165
	.loc 2 1910 0 discriminator 1
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
.LVL1012:
	.loc 2 1909 0 discriminator 1
	cmp	r0, #0
	beq	.L1165
.LBB475:
	.loc 2 1914 0
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1166
	.loc 2 1915 0
	ldr	r0, [r4, #3792]
	.loc 2 1917 0
	movs	r2, #0
	movs	r1, #1
	.loc 2 1915 0
	add	r0, r0, r7
	.loc 2 1916 0
	str	r3, [r0, #4]
	.loc 2 1915 0
	ldr	r8, [r0, #12]
	.loc 2 1917 0
	bl	FlashReadPages
.LVL1013:
	.loc 2 1930 0
	ldr	r2, [r4, #3792]
	ldr	r1, [r2, r7]
	adds	r3, r2, r7
	adds	r1, r1, #1
	bne	.L1167
.LVL1014:
.L1168:
	.loc 2 1962 0
	mov	r3, #-1
.LVL1015:
	.loc 2 1963 0
	ldrh	r0, [fp]
	.loc 2 1962 0
	str	r3, [sp, #36]
	.loc 2 1963 0
	bl	decrement_vpc_count
.LVL1016:
.L1176:
.LBE475:
	.loc 2 2003 0
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1157
.L1189:
	.loc 2 2005 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1017:
	.loc 2 2006 0
	ldrh	r3, [r4, #16]
	.loc 2 2005 0
	mov	r6, r0
.LVL1018:
	.loc 2 2006 0
	cmp	r3, r0
	bhi	.L1181
	.loc 2 2006 0 is_stmt 0 discriminator 1
	movw	r2, #2006
	ldr	r1, .L1250
	ldr	r0, .L1250+4
	bl	printk
.LVL1019:
.L1181:
	.loc 2 2007 0 is_stmt 1
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1182
	.loc 2 2009 0
	mov	r0, r6
	b	.L1244
.LVL1020:
.L1166:
.LBB476:
	.loc 2 1921 0
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1169
	.loc 2 1922 0
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
.LVL1021:
	b	.L1169
.L1167:
	.loc 2 1930 0 discriminator 1
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1168
	.loc 2 1918 0 discriminator 2
	ldr	r9, [r8, #4]
.LVL1022:
	.loc 2 1931 0 discriminator 2
	ldr	r0, [r4, #3964]
	mov	r1, r9
	bl	ftl_cmp_data_ver
.LVL1023:
	.loc 2 1930 0 discriminator 2
	cmp	r0, #0
	beq	.L1168
	.loc 2 1932 0
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1171
.LVL1024:
.L1174:
	.loc 2 1958 0
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
.LVL1025:
	b	.L1168
.LVL1026:
.L1171:
	.loc 2 1935 0
	ldr	r0, [sp, #36]
.LVL1027:
	cmp	r1, r0
	beq	.L1168
	.loc 2 1936 0
	adds	r0, r1, #1
	beq	.L1172
	.loc 2 1938 0
	str	r1, [r3, #4]
	.loc 2 1939 0
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	.loc 2 1937 0
	ldr	r8, [r3, #12]
	.loc 2 1939 0
	bl	FlashReadPages
.LVL1028:
.L1173:
	.loc 2 1944 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1174
	.loc 2 1945 0
	ldr	r3, [r8, #4]
.LVL1029:
	.loc 2 1946 0
	ldr	r0, [r4, #3964]
	mov	r1, r3
	bl	ftl_cmp_data_ver
.LVL1030:
	cmp	r0, #0
	beq	.L1174
	.loc 2 1947 0
	mov	r1, r3
	mov	r0, r9
	bl	ftl_cmp_data_ver
.LVL1031:
	cmp	r0, #0
	beq	.L1168
	b	.L1174
.LVL1032:
.L1172:
	.loc 2 1941 0
	str	r1, [r2, r7]
	b	.L1173
.LVL1033:
.L1165:
.LBE476:
	.loc 2 1964 0
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1176
	.loc 2 1966 0
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
.LVL1034:
	.loc 2 1967 0
	ldr	r8, [sp, #40]
	cmp	r8, #-1
	beq	.L1176
	.loc 2 1967 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L1179
	.loc 2 1968 0 is_stmt 1
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
.LVL1035:
	.loc 2 1970 0
	ldrh	r3, [r4, #2324]
	cmp	r3, r0
	beq	.L1180
	.loc 2 1970 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2372]
	cmp	r3, r0
	beq	.L1180
	.loc 2 1971 0 is_stmt 1
	ldrh	r3, [r4, #2420]
	cmp	r3, r0
	bne	.L1176
.L1180:
.LBB477:
	.loc 2 1976 0
	ldr	r0, [r4, #3792]
	.loc 2 1979 0
	movs	r2, #0
	movs	r1, #1
	.loc 2 1978 0
	str	r8, [r0, #4]
	.loc 2 1976 0
	ldr	r7, [r0, #12]
	.loc 2 1979 0
	bl	FlashReadPages
.LVL1036:
	.loc 2 1982 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1176
	.loc 2 1983 0
	ldr	r1, [r7, #4]
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
.LVL1037:
	cmp	r0, #0
	bne	.L1176
	.loc 2 1990 0
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
.LVL1038:
	b	.L1176
.LVL1039:
.L1182:
.LBE477:
	.loc 2 2011 0
	mov	r1, r6
	ldr	r0, .L1250+8
	bl	printk
.LVL1040:
	b	.L1157
.LVL1041:
.L1161:
	.loc 2 2018 0
	ldr	r3, [r4, #3972]
	cmp	r3, #31
	.loc 2 2019 0
	itttt	ls
	addls	r2, r4, r3, lsl #2
	.loc 2 2020 0
	addls	r3, r3, #1
	strls	r3, [r4, #3972]
	.loc 2 2019 0
	strls	r6, [r2, #3976]
	.loc 2 2022 0
	ldrh	r0, [fp]
.LVL1042:
	bl	decrement_vpc_count
.LVL1043:
	.loc 2 2024 0
	ldr	r3, [r4, #3964]
	adds	r2, r3, #1
	bne	.L1184
	.loc 2 2025 0
	ldr	r3, [sp, #4]
.LVL1044:
.L1243:
	.loc 2 2027 0
	str	r3, [r4, #3964]
	b	.L1157
.LVL1045:
.L1184:
	.loc 2 2026 0
	ldr	r2, [sp, #4]
.LVL1046:
	cmp	r2, r3
	bcs	.L1157
	.loc 2 2027 0
	mov	r3, r2
.LVL1047:
	b	.L1243
.LVL1048:
.L1160:
	.loc 2 2051 0
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
.LVL1049:
	.loc 2 2053 0
	mov	r1, r5
	.loc 2 2052 0
	strh	r5, [fp, #2]	@ movhi
	.loc 2 2053 0
	ldr	r2, [sp, #12]
	.loc 2 2051 0
	strb	r3, [fp, #6]
	b	.L1247
.LVL1050:
.L1179:
	.loc 2 2003 0
	mov	r0, r8
	b	.L1189
.L1251:
	.align	2
.L1250:
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LC98
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
	.loc 2 2205 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1051:
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
.LCFI145:
	.pad #24
	sub	sp, sp, #24
.LCFI146:
	.loc 2 2212 0
	ldr	r4, .L1263
	.loc 2 2211 0
	mov	r3, #-1
	.loc 2 2219 0
	mov	r8, #0
	.loc 2 2220 0
	movw	r1, #61589
	.loc 2 2211 0
	str	r3, [sp, #20]
	.loc 2 2205 0
	mov	r5, r0
	.loc 2 2212 0
	ldr	r2, [r4, #3240]
	.loc 2 2223 0
	movs	r7, #7
	.loc 2 2213 0
	ldr	r6, [r4, #3244]
	.loc 2 2212 0
	str	r2, [sp, #12]
	.loc 2 2216 0
	mvn	r2, #2
	.loc 2 2213 0
	str	r6, [sp, #16]
	.loc 2 2216 0
	str	r2, [r6, #8]
	.loc 2 2217 0
	mvn	r2, #1
	str	r2, [r6, #12]
	.loc 2 2218 0
	ldrh	r2, [r0]
	.loc 2 2219 0
	strh	r8, [r6]	@ movhi
	.loc 2 2218 0
	strh	r2, [r6, #2]	@ movhi
	.loc 2 2220 0
	ldr	r2, [r4, #3240]
	str	r1, [r2]
	.loc 2 2221 0
	add	r1, r1, #304087040
	ldr	r2, [r4, #3240]
	add	r1, r1, #1269760
	addw	r1, r1, #1507
	str	r1, [r2, #4]
.L1253:
	.loc 2 2223 0
	subs	r7, r7, #1
	beq	.L1255
.LVL1052:
	.loc 2 2224 0
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1254
.L1255:
	.loc 2 2234 0
	ldr	r2, [r4, #2300]
	ldrh	r1, [r5]
	ldrh	r0, [r5, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 2 2235 0
	ldrh	r3, [r4, #78]
	strh	r3, [r5, #2]	@ movhi
	.loc 2 2236 0
	movs	r3, #0
	strb	r3, [r5, #6]
	.loc 2 2237 0
	strh	r3, [r5, #4]	@ movhi
	.loc 2 2238 0
	add	sp, sp, #24
.LCFI147:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1053:
.L1254:
.LCFI148:
	.loc 2 2226 0
	mov	r0, r5
	bl	get_new_active_ppa
.LVL1054:
	str	r0, [sp, #8]
	.loc 2 2227 0
	adds	r0, r0, #1
	beq	.L1255
	.loc 2 2229 0
	ldr	r3, [r4, #2520]
	.loc 2 2230 0
	movs	r1, #1
	add	r0, sp, #4
	.loc 2 2229 0
	str	r3, [r6, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r8
	str	r3, [r4, #2520]
	.loc 2 2230 0
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL1055:
	.loc 2 2231 0
	ldrh	r0, [r5]
	bl	decrement_vpc_count
.LVL1056:
	b	.L1253
.L1264:
	.align	2
.L1263:
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
.LCFI149:
	.loc 4 196 0
	movw	r8, #61649
	.loc 4 180 0
	ldr	r4, .L1294
	ldr	r3, [r4, #3240]
	.loc 4 186 0
	addw	r7, r4, #3220
	.loc 4 181 0
	ldr	r6, [r4, #3244]
	.loc 4 180 0
	str	r3, [r4, #3228]
	.loc 4 181 0
	str	r6, [r4, #3232]
	.loc 4 183 0
	bl	FtlBbtMemInit
.LVL1057:
	.loc 4 184 0
	ldrh	r5, [r4, #74]
	subs	r5, r5, #1
	uxth	r5, r5
.LVL1058:
.L1266:
	.loc 4 184 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #74]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L1269
	.loc 4 185 0 is_stmt 1
	lsls	r3, r5, #10
	.loc 4 186 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 185 0
	str	r3, [r4, #3224]
	.loc 4 186 0
	bl	FlashReadPages
.LVL1059:
	.loc 4 187 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	bne	.L1267
	.loc 4 188 0
	ldr	r3, [r4, #3224]
	.loc 4 189 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 188 0
	adds	r3, r3, #1
	str	r3, [r4, #3224]
	.loc 4 189 0
	bl	FlashReadPages
.LVL1060:
.L1267:
	.loc 4 191 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	beq	.L1268
	.loc 4 196 0
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1268
	.loc 4 199 0
	ldr	r3, [r6, #4]
	.loc 4 198 0
	strh	r5, [r4, #128]	@ movhi
	.loc 4 199 0
	str	r3, [r4, #136]
	.loc 4 200 0
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #132]	@ movhi
.L1269:
	.loc 4 216 0
	ldrh	r3, [r4, #128]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1283
	.loc 4 222 0
	ldrh	r3, [r4, #132]
	cmp	r3, r2
	beq	.L1273
	.loc 4 224 0
	lsls	r3, r3, #10
	.loc 4 225 0
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1294+4
	.loc 4 224 0
	str	r3, [r4, #3224]
	.loc 4 225 0
	bl	FlashReadPages
.LVL1061:
	.loc 4 226 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	beq	.L1273
	.loc 4 229 0
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1273
	.loc 4 229 0 is_stmt 0 discriminator 1
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #136]
	cmp	r3, r2
	bls	.L1273
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
.L1273:
	.loc 4 245 0
	ldr	r7, .L1294+4
	.loc 4 238 0
	movs	r1, #1
	ldrh	r0, [r4, #128]
	.loc 4 247 0
	movw	r8, #61649
	.loc 4 238 0
	bl	FtlGetLastWrittenPage
.LVL1062:
	sxth	r5, r0
.LVL1063:
	.loc 4 239 0
	adds	r0, r0, #1
	strh	r0, [r4, #130]	@ movhi
.L1275:
	.loc 4 241 0
	cmp	r5, #0
	bge	.L1278
	.loc 4 254 0
	movs	r2, #254
	ldr	r1, .L1294+8
	ldr	r0, .L1294+12
	bl	printk
.LVL1064:
.L1277:
	.loc 4 255 0
	ldrh	r3, [r6, #10]
	.loc 4 257 0
	ldrh	r0, [r6, #12]
	.loc 4 255 0
	strh	r3, [r4, #134]	@ movhi
	.loc 4 257 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1280
	.loc 4 259 0
	ldr	r2, [r4, #4]
	cmp	r0, r2
	beq	.L1280
	.loc 4 259 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1280
	.loc 4 261 0 is_stmt 1
	cmp	r0, r3
	bcs	.L1280
	.loc 4 263 0
	bl	FtlSysBlkNumInit
.LVL1065:
.L1280:
	ldr	r6, .L1294+16
	.loc 4 173 0 discriminator 1
	movs	r5, #0
.LVL1066:
.L1281:
	.loc 4 268 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	bcc	.L1282
	.loc 4 275 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1067:
.L1268:
	.loc 4 184 0 discriminator 2
	subs	r5, r5, #1
.LVL1068:
	uxth	r5, r5
.LVL1069:
	b	.L1266
.LVL1070:
.L1278:
	.loc 4 243 0
	ldrh	r3, [r4, #128]
	.loc 4 245 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 243 0
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3224]
	.loc 4 244 0
	ldr	r3, [r4, #3240]
	str	r3, [r4, #3228]
	.loc 4 245 0
	bl	FlashReadPages
.LVL1071:
	.loc 4 247 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	beq	.L1276
	.loc 4 247 0 is_stmt 0 discriminator 1
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L1277
.L1276:
	subs	r5, r5, #1
.LVL1072:
	sxth	r5, r5
.LVL1073:
	b	.L1275
.LVL1074:
.L1282:
	.loc 4 269 0 is_stmt 1 discriminator 3
	ldrh	r2, [r4, #3860]
	ldr	r1, [r4, #3228]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	.loc 4 268 0 discriminator 3
	adds	r5, r5, #1
	.loc 4 269 0 discriminator 3
	bl	ftl_memcpy
.LVL1075:
	b	.L1281
.LVL1076:
.L1283:
	.loc 4 218 0
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1077:
.L1295:
	.align	2
.L1294:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
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
.LCFI150:
	.loc 4 327 0
	mov	r8, #0
.LVL1078:
	ldr	r9, .L1315+4
	.loc 4 325 0
	bl	FtlBbtMemInit
.LVL1079:
	.loc 4 327 0
	sub	r5, r9, #156
	sub	r10, r9, #18
	mov	r4, r5
	.loc 4 326 0
	bl	FtlLoadFactoryBbt
.LVL1080:
.L1297:
	.loc 4 327 0 discriminator 1
	ldrh	r3, [r5, #30]
	cmp	r8, r3
	bcc	.L1303
	movs	r5, #0
.L1304:
	.loc 4 381 0 discriminator 1
	ldrh	r3, [r4, #90]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1305
	.loc 4 387 0
	ldrh	r5, [r4, #140]
	.loc 4 396 0
	movw	r6, #65535
	.loc 4 387 0
	subs	r5, r5, #1
	uxth	r5, r5
.L1306:
	.loc 4 387 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #140]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1310
	.loc 4 389 0 is_stmt 1
	mov	r0, r5
	bl	FtlBbmIsBadBlock
.LVL1081:
	cmp	r0, #1
	beq	.L1307
	.loc 4 391 0
	mov	r0, r5
	bl	FlashTestBlk
.LVL1082:
	cmp	r0, #0
	beq	.L1308
	.loc 4 393 0
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.LVL1083:
.L1307:
	.loc 4 387 0 discriminator 2
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1306
.L1303:
	.loc 4 331 0
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	.loc 4 328 0
	ldr	r0, [r5, #3240]
	.loc 4 329 0
	ldr	fp, [r5, #3244]
	.loc 4 331 0
	cmp	r3, r2
	.loc 4 328 0
	str	r0, [r5, #3228]
	.loc 4 329 0
	str	fp, [r5, #3232]
	.loc 4 331 0
	beq	.L1298
	.loc 4 333 0
	ldrh	r7, [r5, #74]
.LVL1084:
	.loc 4 335 0
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1315
	.loc 4 333 0
	mla	r7, r8, r7, r3
.LVL1085:
	.loc 4 334 0
	lsls	r3, r7, #10
	str	r3, [r5, #3224]
	.loc 4 335 0
	bl	FlashReadPages
.LVL1086:
	.loc 4 336 0
	ldrh	r2, [r5, #74]
	ldr	r1, [r5, #3228]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.LVL1087:
.L1299:
	.loc 4 376 0 discriminator 2
	uxth	r0, r7
	.loc 4 327 0 discriminator 2
	add	r8, r8, #1
	add	r9, r9, #4
	.loc 4 376 0 discriminator 2
	bl	FtlBbmMapBadBlock
.LVL1088:
	b	.L1297
.L1298:
	.loc 4 340 0
	mov	r1, r8
	bl	FlashGetBadBlockList
.LVL1089:
	.loc 4 341 0
	ldr	r1, [r9]
	ldr	r0, [r5, #3228]
	bl	FtlBbt2Bitmap
.LVL1090:
	.loc 4 343 0
	ldrh	r6, [r5, #74]
.L1301:
	.loc 4 347 0
	subs	r6, r6, #1
	uxth	r6, r6
.L1300:
	.loc 4 345 0
	ldrh	r0, [r4, #74]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
.LVL1091:
	cmp	r0, #1
	beq	.L1301
.LBB488:
.LBB489:
.LBB490:
	.loc 1 202 0
	movs	r1, #16
	ldr	r0, [r4, #3244]
.LBE490:
.LBE489:
.LBE488:
	.loc 4 349 0
	strh	r6, [r10]	@ movhi
.LBB493:
.LBB492:
.LBB491:
	.loc 1 202 0
	bl	__memzero
.LVL1092:
.LBE491:
.LBE492:
.LBE493:
.LBB494:
.LBB495:
.LBB496:
	mov	r1, #4096
	ldr	r0, [r4, #3240]
	bl	__memzero
.LVL1093:
.LBE496:
.LBE495:
.LBE494:
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
.LVL1094:
	.loc 4 357 0
	ldrh	r2, [r4, #3860]
	.loc 4 354 0
	strh	r3, [fp, #2]	@ movhi
	.loc 4 355 0
	ldrh	r3, [r10]
	.loc 4 357 0
	ldr	r1, [r9]
	lsls	r2, r2, #2
	ldr	r0, [r4, #3228]
	.loc 4 355 0
	mla	r7, r8, r7, r3
.LVL1095:
	.loc 4 356 0
	lsls	r3, r7, #10
	str	r3, [r4, #3224]
	.loc 4 357 0
	bl	ftl_memcpy
.LVL1096:
	.loc 4 358 0
	movs	r2, #1
	ldr	r0, .L1315
	mov	r1, r2
	bl	FlashEraseBlocks
.LVL1097:
	.loc 4 359 0
	movs	r3, #1
	ldr	r0, .L1315
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1098:
	.loc 4 360 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	bne	.L1299
	.loc 4 362 0
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
.LVL1099:
	.loc 4 363 0
	b	.L1300
.L1305:
	.loc 4 383 0 discriminator 3
	bl	FtlBbmMapBadBlock
.LVL1100:
	b	.L1304
.L1308:
	.loc 4 396 0
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	bne	.L1309
	.loc 4 398 0
	strh	r5, [r4, #128]	@ movhi
	b	.L1307
.L1309:
	.loc 4 402 0
	strh	r5, [r4, #132]	@ movhi
.L1310:
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
.LVL1101:
	.loc 4 418 0
	ldrh	r0, [r4, #128]
	bl	FtlBbmMapBadBlock
.LVL1102:
	.loc 4 419 0
	ldrh	r0, [r4, #132]
	bl	FtlBbmMapBadBlock
.LVL1103:
	.loc 4 421 0
	bl	FtlBbmTblFlush
.LVL1104:
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
.LVL1105:
	.loc 4 432 0
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1106:
.L1316:
	.align	2
.L1315:
	.word	.LANCHOR0+3220
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
	.loc 2 835 0
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1107:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI151:
	mov	r10, r2
	.loc 2 843 0
	ldr	r4, .L1329
	adds	r2, r0, r1
.LVL1108:
	.loc 2 835 0
	.pad #108
	sub	sp, sp, #108
.LCFI152:
	.loc 2 835 0
	mov	r8, r0
	mov	r6, r1
	.loc 2 843 0
	ldrh	r3, [r4, #72]
	cmp	r2, r3
	bhi	.L1325
	.loc 2 840 0
	ldrh	r7, [r4, #84]
	mov	r9, #0
	lsr	r7, r0, r7
.LVL1109:
	lsl	fp, r7, #2
.LVL1110:
.L1319:
	.loc 2 846 0
	cbnz	r6, .L1324
.L1317:
	.loc 2 869 0
	mov	r0, r9
	add	sp, sp, #108
.LCFI153:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1111:
.L1324:
.LCFI154:
	.loc 2 847 0
	ldr	r3, [r4, #3844]
	.loc 2 848 0
	mov	r0, r8
	.loc 2 847 0
	ldr	r2, [r3, fp]
.LVL1112:
	.loc 2 848 0
	ldrh	r3, [r4, #34]
	.loc 2 847 0
	str	r2, [sp, #12]
	.loc 2 848 0
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
.LVL1113:
	.loc 2 849 0
	ldr	r3, [sp, #8]
	.loc 2 850 0
	ldr	r2, [sp, #12]
.LVL1114:
	.loc 2 848 0
	str	r1, [sp, #4]
	.loc 2 849 0
	subs	r5, r3, r1
.LVL1115:
	uxth	r5, r5
.LVL1116:
	.loc 2 850 0
	cmp	r6, r5
	.loc 2 851 0
	it	cc
	uxthcc	r5, r6
	.loc 2 852 0
	cbz	r2, .L1321
	.loc 2 852 0 is_stmt 0 discriminator 1
	cmp	r5, r3
	beq	.L1321
	.loc 2 854 0 is_stmt 1
	ldr	r3, [r4, #3808]
	.loc 2 856 0
	add	r0, sp, #20
	.loc 2 853 0
	str	r2, [sp, #24]
	.loc 2 856 0
	movs	r2, #1
.LVL1117:
	mov	r1, r2
	.loc 2 854 0
	str	r3, [sp, #28]
	.loc 2 855 0
	add	r3, sp, #40
	str	r3, [sp, #32]
	.loc 2 856 0
	bl	FlashReadPages
.LVL1118:
.L1322:
	.loc 2 860 0
	lsls	r3, r5, #9
	ldr	r0, [r4, #3808]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	.loc 2 864 0
	subs	r6, r6, r5
	.loc 2 865 0
	add	r8, r8, r5
	add	fp, fp, #4
	.loc 2 860 0
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
.LVL1119:
	.loc 2 861 0
	mov	r1, r7
	ldr	r2, [r4, #3808]
	ldr	r0, .L1329+4
	.loc 2 863 0
	adds	r7, r7, #1
	.loc 2 861 0
	bl	FtlMapWritePage
.LVL1120:
	.loc 2 866 0
	ldr	r3, [sp, #8]
	.loc 2 862 0
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	.loc 2 866 0
	add	r10, r10, r3
	b	.L1319
.LVL1121:
.L1321:
	.loc 2 858 0
	ldrh	r2, [r4, #86]
.LVL1122:
	movs	r1, #0
	ldr	r0, [r4, #3808]
	bl	ftl_memset
.LVL1123:
	b	.L1322
.LVL1124:
.L1325:
	.loc 2 844 0
	mov	r9, #-1
	b	.L1317
.L1330:
	.align	2
.L1329:
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
	.loc 2 2159 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 2 2160 0
	ldr	r2, .L1333
	ldr	r3, .L1333+4
	ldr	r1, [r2, #2612]
	cmp	r1, r3
	bne	.L1331
.LBB499:
.LBB500:
	.loc 2 2161 0
	ldr	r3, .L1333+8
	.loc 2 2174 0
	addw	r2, r2, #2612
	movs	r1, #1
	movs	r0, #0
	.loc 2 2161 0
	str	r3, [r2, #4]
	.loc 2 2162 0
	ldr	r3, [r2, #-104]
	str	r3, [r2, #88]
	.loc 2 2163 0
	ldr	r3, [r2, #-100]
	str	r3, [r2, #92]
	.loc 2 2164 0
	ldr	r3, [r2, #-108]
	str	r3, [r2, #8]
	.loc 2 2165 0
	ldr	r3, [r2, #-120]
	str	r3, [r2, #12]
	.loc 2 2166 0
	ldr	r3, [r2, #-128]
	str	r3, [r2, #16]
	.loc 2 2167 0
	ldr	r3, [r2, #-112]
	str	r3, [r2, #20]
	.loc 2 2168 0
	ldr	r3, [r2, #-84]
	str	r3, [r2, #28]
	.loc 2 2169 0
	ldr	r3, [r2, #-76]
	str	r3, [r2, #32]
	.loc 2 2170 0
	ldr	r3, [r2, #-124]
	str	r3, [r2, #36]
	.loc 2 2171 0
	ldr	r3, [r2, #-116]
	str	r3, [r2, #40]
	.loc 2 2172 0
	ldr	r3, [r2, #-72]
	str	r3, [r2, #44]
	.loc 2 2173 0
	ldr	r3, [r2, #-68]
	str	r3, [r2, #48]
	.loc 2 2174 0
	b	FtlVendorPartWrite
.LVL1125:
.L1331:
	bx	lr
.L1334:
	.align	2
.L1333:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177348
.LBE500:
.LBE499:
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
	.loc 2 910 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1126:
	.loc 2 913 0
	ldr	r2, .L1340
	.loc 2 910 0
	push	{r3, lr}
	.save {r3, lr}
.LCFI155:
	.loc 2 913 0
	ldrh	r3, [r2, #-240]
	cmp	r3, #31
	.loc 2 914 0
	itett	ls
	addls	r3, r3, #1
	.loc 2 911 0
	movhi	r2, #32
.LVL1127:
	.loc 2 914 0
	strhls	r3, [r2, #-240]	@ movhi
.LVL1128:
	.loc 2 915 0
	movls	r2, #1
.LVL1129:
	ldr	r3, .L1340+4
	.loc 2 917 0
	cbnz	r0, .L1337
	.loc 2 917 0 is_stmt 0 discriminator 1
	ldr	r1, [r3, #3820]
	ldr	r0, [r1, #20]
.LVL1130:
	ldr	r1, [r1, #16]
	add	r2, r2, r0
.LVL1131:
	cmp	r1, r2
	bcc	.L1338
.LVL1132:
.L1337:
	.loc 2 919 0 is_stmt 1
	ldr	r2, [r3, #3820]
.LVL1133:
	.loc 2 924 0
	movs	r0, #64
.LVL1134:
	.loc 2 919 0
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	.loc 2 920 0
	ldr	r1, .L1340+8
	str	r1, [r2]
	.loc 2 921 0
	ldrh	r1, [r3, #3812]
	ldr	r2, [r3, #3820]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	.loc 2 922 0
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	.loc 2 923 0
	movs	r3, #0
	str	r3, [r2, #4]
	.loc 2 924 0
	bl	FtlVendorPartWrite
.LVL1135:
	.loc 2 925 0
	bl	Ftl_save_ext_data
.LVL1136:
.L1338:
	.loc 2 927 0
	movs	r0, #0
	pop	{r3, pc}
.L1341:
	.align	2
.L1340:
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
	.loc 2 872 0
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1137:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI156:
	mov	r10, r2
	.loc 2 881 0
	ldr	r5, .L1351
	adds	r2, r0, r1
.LVL1138:
	.loc 2 872 0
	.pad #100
	sub	sp, sp, #100
.LCFI157:
	.loc 2 872 0
	mov	r8, r0
	mov	r7, r1
	.loc 2 881 0
	ldrh	r3, [r5, #72]
	cmp	r2, r3
	bhi	.L1350
	.loc 2 877 0
	ldrh	r6, [r5, #84]
	mov	r9, #0
	lsr	r6, r0, r6
.LVL1139:
	lsls	r6, r6, #2
.LVL1140:
.L1344:
	.loc 2 883 0
	cbnz	r7, .L1349
.L1342:
	.loc 2 906 0
	mov	r0, r9
	add	sp, sp, #100
.LCFI158:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1141:
.L1349:
.LCFI159:
	.loc 2 884 0
	ldr	r3, [r5, #3844]
	.loc 2 885 0
	mov	r0, r8
	ldrh	r4, [r5, #34]
.LVL1142:
	.loc 2 884 0
	ldr	r3, [r3, r6]
.LVL1143:
	.loc 2 885 0
	mov	r1, r4
	.loc 2 884 0
	str	r3, [sp, #4]
	.loc 2 885 0
	bl	__aeabi_uidivmod
.LVL1144:
	.loc 2 886 0
	subs	r4, r4, r1
	.loc 2 887 0
	ldr	r3, [sp, #4]
.LVL1145:
	.loc 2 886 0
	uxth	r4, r4
.LVL1146:
	.loc 2 885 0
	str	r1, [sp]
	.loc 2 887 0
	cmp	r7, r4
	.loc 2 888 0
	it	cc
	uxthcc	r4, r7
	lsl	fp, r4, #9
	.loc 2 889 0
	cbz	r3, .L1346
	.loc 2 890 0
	str	r3, [sp, #16]
	.loc 2 893 0
	movs	r2, #1
	.loc 2 891 0
	ldr	r3, [r5, #3808]
.LVL1147:
	.loc 2 893 0
	mov	r1, r2
	add	r0, sp, #12
	.loc 2 891 0
	str	r3, [sp, #20]
	.loc 2 892 0
	add	r3, sp, #32
	str	r3, [sp, #24]
	.loc 2 893 0
	bl	FlashReadPages
.LVL1148:
	.loc 2 894 0
	ldr	r3, [sp, #12]
	.loc 2 896 0
	mov	r2, fp
	ldr	r1, [r5, #3808]
	mov	r0, r10
	.loc 2 895 0
	adds	r3, r3, #1
	.loc 2 896 0
	ldr	r3, [sp]
	.loc 2 895 0
	it	eq
	moveq	r9, #-1
	.loc 2 896 0
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.LVL1149:
.L1348:
	.loc 2 901 0
	subs	r7, r7, r4
	.loc 2 902 0
	add	r8, r8, r4
	.loc 2 903 0
	add	r10, r10, fp
	adds	r6, r6, #4
	b	.L1344
.LVL1150:
.L1346:
	.loc 2 898 0
	mov	r2, fp
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
.LVL1151:
	b	.L1348
.LVL1152:
.L1350:
	.loc 2 882 0
	mov	r9, #-1
	b	.L1342
.L1352:
	.align	2
.L1351:
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
	.loc 2 1383 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI160:
	.loc 2 1384 0
	movs	r0, #64
	ldr	r4, .L1355
	ldr	r2, [r4, #3820]
	ldrh	r1, [r4, #3812]
	bl	FtlVendorPartRead
.LVL1153:
	.loc 2 1385 0
	ldr	r3, [r4, #3820]
	ldr	r2, [r3]
	ldr	r3, .L1355+4
	cmp	r2, r3
	beq	.L1354
.LBB503:
.LBB504:
	.loc 2 1387 0
	ldr	r1, .L1355+8
	ldr	r0, .L1355+12
	bl	printk
.LVL1154:
	.loc 2 1388 0
	ldrh	r2, [r4, #3812]
	movs	r1, #0
	ldr	r0, [r4, #3820]
	lsls	r2, r2, #9
	bl	ftl_memset
.LVL1155:
.L1354:
.LBE504:
.LBE503:
	.loc 2 1393 0
	movs	r0, #0
	pop	{r4, pc}
.L1356:
	.align	2
.L1355:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC99
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
	.loc 2 2179 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
.LCFI161:
	.loc 2 2180 0
	movs	r1, #1
	ldr	r4, .L1360
	movs	r0, #0
	.loc 2 2181 0
	ldr	r5, .L1360+4
	.loc 2 2180 0
	addw	r2, r4, #2612
	bl	FtlVendorPartRead
.LVL1156:
	.loc 2 2181 0
	ldr	r3, [r4, #2612]
	cmp	r3, r5
	beq	.L1358
.LBB510:
.LBB511:
.LBB512:
	.loc 1 202 0
	mov	r1, #512
	addw	r0, r4, #2612
	bl	__memzero
.LVL1157:
.LBE512:
.LBE511:
.LBE510:
	.loc 2 2183 0
	str	r5, [r4, #2612]
.L1358:
	.loc 2 2186 0
	ldr	r3, [r4, #2612]
	cmp	r3, r5
	bne	.L1359
	.loc 2 2187 0
	ldr	r3, [r4, #2700]
	str	r3, [r4, #2508]
	.loc 2 2188 0
	ldr	r3, [r4, #2704]
	str	r3, [r4, #2512]
	.loc 2 2189 0
	ldr	r3, [r4, #2620]
	str	r3, [r4, #2504]
	.loc 2 2190 0
	ldr	r3, [r4, #2624]
	str	r3, [r4, #2492]
	.loc 2 2191 0
	ldr	r3, [r4, #2628]
	str	r3, [r4, #2484]
	.loc 2 2192 0
	ldr	r3, [r4, #2632]
	str	r3, [r4, #2500]
	.loc 2 2193 0
	ldr	r3, [r4, #2640]
	str	r3, [r4, #2528]
	.loc 2 2194 0
	ldr	r3, [r4, #2644]
	str	r3, [r4, #2536]
	.loc 2 2195 0
	ldr	r3, [r4, #2648]
	str	r3, [r4, #2488]
	.loc 2 2196 0
	ldr	r3, [r4, #2652]
	str	r3, [r4, #2496]
	.loc 2 2197 0
	ldr	r3, [r4, #2656]
	str	r3, [r4, #2540]
	.loc 2 2198 0
	ldr	r3, [r4, #2660]
	str	r3, [r4, #2544]
.L1359:
	.loc 2 2201 0
	ldrh	r2, [r4, #68]
	ldr	r3, [r4, #2528]
	ldr	r0, [r4, #2524]
	ldrh	r1, [r4, #16]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
.LVL1158:
	str	r0, [r4, #2532]
	pop	{r3, r4, r5, pc}
.L1361:
	.align	2
.L1360:
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
	.loc 2 1053 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
.LCFI162:
	.loc 2 1065 0
	movs	r5, #0
	.loc 2 1059 0
	ldr	r4, .L1374
	.loc 2 1096 0
	movs	r1, #255
	.loc 2 1054 0
	movw	r7, #65535
.LVL1159:
	.loc 2 1124 0
	ldr	r9, .L1374+16
	.loc 2 1059 0
	ldr	r3, [r4, #3240]
	.loc 2 1105 0
	addw	r8, r4, #3220
	.loc 2 1060 0
	ldr	r6, [r4, #3244]
	.loc 2 1124 0
	ldr	r10, .L1374+20
	.loc 2 1059 0
	str	r3, [r4, #3228]
	.loc 2 1062 0
	ldrh	r3, [r4, #2552]
	.loc 2 1060 0
	str	r6, [r4, #3232]
	.loc 2 1066 0
	str	r5, [r6, #12]
	.loc 2 1062 0
	strh	r3, [r6, #2]	@ movhi
	.loc 2 1063 0
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	.loc 2 1064 0
	ldr	r3, [r4, #2560]
	.loc 2 1065 0
	str	r5, [r6, #8]
	.loc 2 1074 0
	ldrh	r2, [r4, #2326]
	.loc 2 1064 0
	str	r3, [r6, #4]
	.loc 2 1068 0
	ldr	r3, .L1374+4
	str	r3, [r4, #2244]
	.loc 2 1069 0
	ldr	r3, .L1374+8
	str	r3, [r4, #2248]
	.loc 2 1070 0
	ldrh	r3, [r4, #2558]
	strh	r3, [r4, #2252]	@ movhi
	.loc 2 1071 0
	ldrh	r3, [r4, #30]
	strb	r3, [r4, #2254]
	.loc 2 1073 0
	ldrh	r3, [r4, #2324]
	strh	r3, [r4, #2258]	@ movhi
	.loc 2 1074 0
	ldrb	r3, [r4, #2330]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	.loc 2 1078 0
	ldrh	r2, [r4, #2374]
	.loc 2 1074 0
	strh	r3, [r4, #2260]	@ movhi
	.loc 2 1075 0
	ldrb	r3, [r4, #2332]	@ zero_extendqisi2
	strb	r3, [r4, #2255]
	.loc 2 1077 0
	ldrh	r3, [r4, #2372]
	strh	r3, [r4, #2262]	@ movhi
	.loc 2 1078 0
	ldrb	r3, [r4, #2378]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2264]	@ movhi
	.loc 2 1079 0
	ldrb	r3, [r4, #2380]	@ zero_extendqisi2
	strb	r3, [r4, #2256]
	.loc 2 1081 0
	ldrh	r3, [r4, #2420]
	.loc 2 1082 0
	ldrh	r2, [r4, #2422]
	.loc 2 1096 0
	ldr	r0, [r4, #3228]
	.loc 2 1081 0
	strh	r3, [r4, #2266]	@ movhi
	.loc 2 1082 0
	ldrb	r3, [r4, #2426]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	.loc 2 1096 0
	ldrh	r2, [r4, #86]
	.loc 2 1082 0
	strh	r3, [r4, #2268]	@ movhi
	.loc 2 1083 0
	ldrb	r3, [r4, #2428]	@ zero_extendqisi2
	strb	r3, [r4, #2257]
	.loc 2 1085 0
	ldr	r3, [r4, #2524]
	str	r3, [r4, #2276]
	.loc 2 1090 0
	ldr	r3, [r4, #2516]
	str	r3, [r4, #2284]
	.loc 2 1091 0
	ldr	r3, [r4, #2520]
	str	r3, [r4, #2280]
	.loc 2 1096 0
	bl	ftl_memset
.LVL1160:
	.loc 2 1097 0
	movs	r2, #48
	addw	r1, r4, #2244
	ldr	r0, [r4, #3228]
	bl	ftl_memcpy
.LVL1161:
	.loc 2 1098 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #3228]
	ldr	r1, [r4, #2300]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
.LVL1162:
	.loc 2 1099 0
	ldrh	r0, [r4, #16]
	ldr	r3, [r4, #3228]
	ldr	r1, [r4]
	lsrs	r2, r0, #3
	adds	r0, r0, #24
	lsls	r0, r0, #1
	adds	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r0, r3
	bl	ftl_memcpy
.LVL1163:
	.loc 2 1100 0
	mov	r0, r5
	bl	FtlUpdateVaildLpn
.LVL1164:
.L1363:
	.loc 2 1102 0
	ldr	r3, [r4, #3240]
	.loc 2 1105 0
	mov	r0, r8
	.loc 2 1104 0
	ldrh	r2, [r4, #2552]
	.loc 2 1102 0
	str	r3, [r4, #3228]
	.loc 2 1103 0
	ldr	r3, [r4, #3244]
	str	r3, [r4, #3232]
	.loc 2 1104 0
	ldrh	r3, [r4, #2554]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3224]
	.loc 2 1105 0
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1165:
	.loc 2 1106 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r4, #2554]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1364
	.loc 2 1108 0
	ldrh	r3, [r4, #2552]
	.loc 2 1107 0
	ldrh	r7, [r4, #2556]
	.loc 2 1108 0
	strh	r3, [r4, #2556]	@ movhi
	.loc 2 1109 0
	movs	r3, #0
	strh	r3, [r4, #2554]	@ movhi
	.loc 2 1110 0
	bl	FtlFreeSysBlkQueueOut
.LVL1166:
	.loc 2 1115 0
	ldr	r3, [r4, #2516]
	.loc 2 1110 0
	strh	r0, [r4, #2552]	@ movhi
	.loc 2 1115 0
	adds	r2, r3, #1
	str	r3, [r4, #2560]
	str	r2, [r4, #2516]
	.loc 2 1116 0
	lsls	r2, r0, #10
	str	r2, [r4, #3224]
	.loc 2 1117 0
	str	r3, [r6, #4]
	.loc 2 1119 0
	movs	r3, #1
	.loc 2 1118 0
	strh	r0, [r6, #2]	@ movhi
	.loc 2 1119 0
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
.LVL1167:
.L1364:
	.loc 2 1121 0
	ldrh	r3, [r4, #2554]
	.loc 2 1122 0
	ldr	r2, [r4, #3220]
	.loc 2 1121 0
	adds	r3, r3, #1
	uxth	r3, r3
	.loc 2 1122 0
	adds	r2, r2, #1
	.loc 2 1121 0
	strh	r3, [r4, #2554]	@ movhi
	.loc 2 1122 0
	bne	.L1365
	.loc 2 1124 0
	cmp	r3, #1
	bne	.L1366
	.loc 2 1124 0 is_stmt 0 discriminator 1
	movw	r2, #1124
	mov	r1, r9
	mov	r0, r10
	bl	printk
.LVL1168:
.L1366:
	.loc 2 1125 0 is_stmt 1
	ldrh	r3, [r4, #2554]
	.loc 2 1127 0
	adds	r5, r5, #1
.LVL1169:
	uxth	r5, r5
.LVL1170:
	.loc 2 1125 0
	cmp	r3, #1
	.loc 2 1126 0
	ittt	eq
	ldrheq	r3, [r4, #80]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2554]	@ movhi
	.loc 2 1128 0
	cmp	r5, #3
	bls	.L1363
	.loc 2 1129 0
	mov	r2, r5
	ldr	r1, [r4, #3224]
	ldr	r0, .L1374+12
	bl	printk
.LVL1171:
.L1369:
	b	.L1369
.L1365:
	.loc 2 1135 0
	cmp	r3, #1
	beq	.L1363
	.loc 2 1138 0
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1370
	.loc 2 1139 0
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.LVL1172:
.L1370:
	.loc 2 1141 0
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.LVL1173:
.L1375:
	.align	2
.L1374:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177348
	.word	.LC100
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
	.loc 2 2242 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
.LCFI163:
	.loc 2 2249 0
	movs	r5, #0
.LVL1174:
	.loc 2 2248 0
	ldr	r4, .L1388
	.loc 2 2247 0
	ldr	r1, .L1388+4
	ldr	r0, .L1388+8
	bl	printk
.LVL1175:
	.loc 2 2248 0
	ldrh	r2, [r4, #18]
	movs	r1, #0
	ldr	r0, [r4, #3824]
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL1176:
.L1377:
	.loc 2 2249 0 discriminator 1
	ldr	r3, [r4, #2480]
	cmp	r5, r3
	bcc	.L1379
	.loc 2 2259 0 discriminator 1
	ldr	r9, .L1388+12
	movs	r7, #0
	.loc 2 2257 0 discriminator 1
	movw	r8, #65535
.LVL1177:
.L1380:
	.loc 2 2256 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r5, r7
.LVL1178:
	cmp	r3, r5
	bhi	.L1384
	.loc 2 2268 0
	add	sp, sp, #12
.LCFI164:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.LVL1179:
.L1379:
.LCFI165:
	.loc 2 2250 0
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
.LVL1180:
	.loc 2 2251 0
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1378
	.loc 2 2252 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1181:
	.loc 2 2253 0
	ldr	r2, [r4, #3824]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1378:
	.loc 2 2249 0 discriminator 2
	adds	r5, r5, #1
	b	.L1377
.LVL1182:
.L1384:
	.loc 2 2257 0
	ldr	r3, [r4, #2300]
	uxth	r6, r7
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #3824]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r2, r3
	beq	.L1382
	.loc 2 2257 0 is_stmt 0 discriminator 1
	cmp	r2, r8
	beq	.L1382
	.loc 2 2259 0 is_stmt 1
	mov	r1, r6
	mov	r0, r9
	bl	printk
.LVL1183:
	.loc 2 2260 0
	ldrh	r3, [r4, #2324]
	cmp	r3, r5
	beq	.L1382
	.loc 2 2260 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2420]
	cmp	r3, r5
	beq	.L1382
	.loc 2 2260 0 discriminator 2
	ldrh	r3, [r4, #2372]
	cmp	r3, r5
	beq	.L1382
	.loc 2 2261 0 is_stmt 1
	ldr	r3, [r4, #3824]
	.loc 2 2262 0
	mov	r0, r5
	.loc 2 2261 0
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #2300]
	strh	r2, [r3, r6, lsl #1]	@ movhi
	.loc 2 2262 0
	bl	update_vpc_list
.LVL1184:
	.loc 2 2263 0
	bl	l2p_flush
.LVL1185:
	.loc 2 2264 0
	bl	FtlVpcTblFlush
.LVL1186:
.L1382:
	adds	r7, r7, #1
	b	.L1380
.L1389:
	.align	2
.L1388:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC90
	.word	.LC101
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
	.loc 2 2679 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1187:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI166:
	.loc 2 2679 0
	mov	r6, r0
	.loc 2 2681 0
	ldr	r4, .L1400
	.loc 2 2680 0
	ldrh	r5, [r0]
.LVL1188:
	.loc 2 2681 0
	ldrh	r3, [r4, #16]
	cmp	r3, r5
	bcs	.L1391
	.loc 2 2681 0 is_stmt 0 discriminator 1
	movw	r2, #2681
	ldr	r1, .L1400+4
	ldr	r0, .L1400+8
	bl	printk
.LVL1189:
.L1391:
	.loc 2 2683 0 is_stmt 1
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1392
	.loc 2 2684 0
	ldr	r3, [r4, #2300]
	.loc 2 2685 0
	mov	r0, r5
	.loc 2 2684 0
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1393
	.loc 2 2685 0
	bl	INSERT_DATA_LIST
.LVL1190:
.L1392:
	.loc 2 2690 0
	ldrh	r0, [r4, #3952]
	.loc 2 2689 0
	movs	r3, #1
	strb	r3, [r6, #8]
	.loc 2 2690 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1394
	.loc 2 2691 0
	cmp	r5, r0
	bne	.L1395
	.loc 2 2691 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1396
.L1395:
	.loc 2 2692 0 is_stmt 1
	bl	update_vpc_list
.LVL1191:
.L1396:
	.loc 2 2693 0
	movw	r3, #65535
	strh	r3, [r4, #3952]	@ movhi
.L1394:
	.loc 2 2695 0
	mov	r0, r6
	bl	allocate_data_superblock
.LVL1192:
	.loc 2 2696 0
	bl	l2p_flush
.LVL1193:
	.loc 2 2697 0
	movs	r0, #0
	bl	FtlEctTblFlush
.LVL1194:
	.loc 2 2698 0
	bl	FtlVpcTblFlush
.LVL1195:
	.loc 2 2700 0
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.LVL1196:
.L1393:
	.loc 2 2687 0
	bl	INSERT_FREE_LIST
.LVL1197:
	b	.L1392
.L1401:
	.align	2
.L1400:
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
	.loc 3 904 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1198:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI167:
	.loc 3 913 0
	movs	r7, #0
.LVL1199:
	ldr	r6, .L1416
	.loc 3 904 0
	.pad #20
	sub	sp, sp, #20
.LCFI168:
	.loc 3 904 0
	mov	r4, r3
	.loc 3 911 0
	movs	r2, #0
.LVL1200:
	.loc 3 933 0
	ldr	r10, .L1416+8
	.loc 3 904 0
	mov	r5, r0
	.loc 3 933 0
	ldr	fp, .L1416+12
	.loc 3 904 0
	mov	r9, r1
	.loc 3 911 0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
.LVL1201:
	bl	FlashProgPages
.LVL1202:
.L1403:
	.loc 3 913 0 discriminator 1
	cmp	r7, r9
	beq	.L1410
	.loc 3 919 0
	mov	r8, #0
	b	.L1411
.LVL1203:
.L1406:
	.loc 3 915 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1204:
	.loc 3 916 0
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1404
	.loc 3 917 0
	ldr	r1, [r6, #2300]
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	.loc 3 918 0
	ldrh	r3, [r6, #78]
	.loc 3 919 0
	strb	r8, [r4, #6]
	.loc 3 920 0
	strh	r8, [r4, #4]	@ movhi
	.loc 3 918 0
	strh	r3, [r4, #2]	@ movhi
.L1404:
	.loc 3 922 0
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1405
	.loc 3 923 0
	mov	r0, r4
	bl	allocate_new_data_superblock
.LVL1205:
.L1405:
	.loc 3 926 0
	ldr	r3, [r6, #2708]
	adds	r3, r3, #1
	str	r3, [r6, #2708]
	.loc 3 927 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
.LVL1206:
	.loc 3 928 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1207:
	.loc 3 930 0
	movs	r2, #0
	.loc 3 929 0
	str	r0, [r5, #4]
	.loc 3 928 0
	str	r0, [sp, #12]
	.loc 3 930 0
	movs	r1, #1
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r0, r5
	bl	FlashProgPages
.LVL1208:
.L1411:
	.loc 3 914 0
	ldr	r3, [r5]
.LVL1209:
	adds	r3, r3, #1
	beq	.L1406
	.loc 3 933 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
	cmp	r2, r3
	bcc	.L1407
	.loc 3 933 0 is_stmt 0 discriminator 1
	movw	r2, #933
	mov	r1, r10
	mov	r0, fp
	bl	printk
.LVL1210:
.L1407:
	.loc 3 934 0 is_stmt 1
	ldr	r3, [r5, #4]
	add	r1, sp, #16
	.loc 3 935 0
	movs	r2, #1
	ldr	r0, [r5, #16]
	.loc 3 934 0
	str	r3, [r1, #-4]!
.LVL1211:
	.loc 3 935 0
	bl	log2phys
.LVL1212:
	.loc 3 937 0
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #12]
.LVL1213:
	.loc 3 939 0
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #4]
	bl	P2V_block_in_plane
.LVL1214:
	.loc 3 940 0
	ldr	r3, [sp, #4]
.LVL1215:
	.loc 3 939 0
	mov	r8, r0
	.loc 3 940 0
	adds	r3, r3, #1
	beq	.L1408
	.loc 3 942 0
	ldr	r3, [r6, #2300]
.LVL1216:
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1409
	.loc 3 943 0
	mov	r1, r0
	ldr	r0, .L1416+4
	bl	printk
.LVL1217:
.L1409:
	.loc 3 944 0
	mov	r0, r8
	bl	decrement_vpc_count
.LVL1218:
.L1408:
	.loc 3 913 0 discriminator 2
	adds	r7, r7, #1
	adds	r5, r5, #20
	b	.L1403
.LVL1219:
.L1410:
	.loc 3 948 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
.LVL1220:
	cmp	r2, r3
	bcc	.L1402
	.loc 3 948 0 is_stmt 0 discriminator 1
	mov	r2, #948
	ldr	r1, .L1416+8
	ldr	r0, .L1416+12
	bl	printk
.LVL1221:
.L1402:
	.loc 3 949 0 is_stmt 1
	add	sp, sp, #20
.LCFI169:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1222:
.L1417:
	.align	2
.L1416:
	.word	.LANCHOR0
	.word	.LC102
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
	.loc 3 952 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
.LCFI170:
	.loc 3 953 0
	ldr	r4, .L1423
	ldr	r0, [r4, #3948]
	cbz	r0, .L1419
	.loc 3 954 0
	ldr	r3, .L1423+4
	movs	r2, #0
	movs	r1, #1
	ldr	r3, [r3, #-236]
	bl	FtlProgPages
.LVL1223:
	.loc 3 955 0
	movs	r3, #0
	str	r3, [r4, #3948]
.L1419:
	.loc 3 958 0
	movs	r0, #0
	pop	{r4, pc}
.L1424:
	.align	2
.L1423:
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
.LCFI171:
	.loc 3 798 0
	bl	FtlCacheWriteBack
.LVL1224:
	.loc 3 799 0
	bl	l2p_flush
.LVL1225:
	.loc 3 800 0
	movs	r0, #1
	bl	FtlEctTblFlush
.LVL1226:
	.loc 3 801 0
	bl	FtlVpcTblFlush
.LVL1227:
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
.LCFI172:
	.loc 3 807 0
	ldr	r3, .L1428
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1427
	.loc 3 808 0
	bl	FtlSysFlush
.LVL1228:
.L1427:
	.loc 3 810 0
	movs	r0, #0
	pop	{r3, pc}
.L1429:
	.align	2
.L1428:
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
	.loc 3 1157 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1229:
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
.LCFI173:
.LVL1230:
	.loc 3 1164 0
	adds	r2, r0, r1
	ldr	r6, .L1445
	.loc 3 1157 0
	mov	r7, r0
	mov	r5, r1
	.loc 3 1164 0
	ldr	r3, [r6, #116]
	cmp	r2, r3
	bhi	.L1438
	.loc 3 1167 0
	cmp	r1, #31
	bhi	.L1432
.LVL1231:
.L1437:
	.loc 3 1168 0
	movs	r0, #0
.L1430:
	.loc 3 1201 0
	add	sp, sp, #8
.LCFI174:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.LVL1232:
.L1432:
.LCFI175:
	.loc 3 1170 0
	bl	FtlCacheWriteBack
.LVL1233:
	.loc 3 1172 0
	ldrh	r4, [r6, #34]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidiv
.LVL1234:
	.loc 3 1173 0
	smulbb	r3, r0, r4
	.loc 3 1172 0
	mov	r8, r0
.LVL1235:
	.loc 3 1173 0
	subs	r7, r7, r3
.LVL1236:
	uxth	r7, r7
.LVL1237:
	.loc 3 1174 0
	cbz	r7, .L1433
	.loc 3 1175 0
	subs	r4, r4, r7
	.loc 3 1176 0
	add	r8, r0, #1
	.loc 3 1175 0
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	.loc 3 1177 0
	subs	r5, r5, r4
.L1433:
	.loc 3 1184 0
	ldr	r4, .L1445+4
	.loc 3 1180 0
	mov	r3, #-1
	str	r3, [sp, #4]
.LVL1238:
.L1434:
	.loc 3 1181 0
	ldrh	r3, [r6, #34]
	cmp	r5, r3
	bcs	.L1436
	.loc 3 1194 0
	ldr	r3, .L1445+4
	ldr	r2, [r3, #-232]
	cmp	r2, #32
	bls	.L1437
	.loc 3 1196 0
	movs	r4, #0
	str	r4, [r3, #-232]
	.loc 3 1197 0
	bl	l2p_flush
.LVL1239:
	.loc 3 1198 0
	bl	FtlVpcTblFlush
.LVL1240:
	b	.L1437
.L1436:
	.loc 3 1182 0
	movs	r2, #0
	mov	r1, sp
	mov	r0, r8
	bl	log2phys
.LVL1241:
	.loc 3 1183 0
	ldr	r3, [sp]
	adds	r3, r3, #1
.LVL1242:
	beq	.L1435
	.loc 3 1184 0
	ldr	r3, [r4, #-232]
.LVL1243:
	.loc 3 1186 0
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r8
	.loc 3 1184 0
	adds	r3, r3, #1
	str	r3, [r4, #-232]
	.loc 3 1185 0
	ldr	r3, [r6, #2488]
	adds	r3, r3, #1
	str	r3, [r6, #2488]
	.loc 3 1186 0
	bl	log2phys
.LVL1244:
	.loc 3 1187 0
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1245:
	.loc 3 1188 0
	bl	decrement_vpc_count
.LVL1246:
.L1435:
	.loc 3 1191 0
	ldrh	r3, [r6, #34]
.LVL1247:
	.loc 3 1190 0
	add	r8, r8, #1
	.loc 3 1191 0
	subs	r5, r5, r3
	b	.L1434
.LVL1248:
.L1438:
	.loc 3 1165 0
	mov	r0, #-1
	b	.L1430
.L1446:
	.align	2
.L1445:
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
.LVL1249:
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
.LCFI176:
.LVL1250:
	.loc 5 119 0
	movw	r2, #65535
	.loc 5 115 0
	ldr	r4, .L1473
	.loc 5 119 0
	ldrh	r5, [r4, #2420]
	.loc 5 115 0
	ldrh	r1, [r4, #78]
.LVL1251:
	.loc 5 119 0
	cmp	r5, r2
	bne	.L1448
.LVL1252:
.L1458:
	.loc 5 153 0
	ldrh	r2, [r4, #2420]
	.loc 5 152 0
	movs	r3, #0
	str	r3, [r4, #3960]
	.loc 5 153 0
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1471
.L1449:
	.loc 5 154 0
	movs	r0, #0
.L1447:
	.loc 5 199 0
	add	sp, sp, #12
.LCFI177:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1253:
.L1448:
.LCFI178:
	.loc 5 121 0
	cbz	r0, .L1451
	.loc 5 123 0
	ldr	r3, .L1473+4
	ldrh	r0, [r3, #4]
.LVL1254:
	cmp	r0, r2
	beq	.L1452
.L1453:
	.loc 5 122 0
	movs	r1, #2
.LVL1255:
.L1451:
	.loc 5 131 0
	ldr	r0, .L1473+8
.LVL1256:
	bl	FtlGcScanTempBlk
.LVL1257:
	str	r0, [sp, #4]
.LVL1258:
	.loc 5 132 0
	adds	r0, r0, #1
	beq	.L1454
	.loc 5 135 0
	ldr	r2, [r4, #2304]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L1455
	.loc 5 136 0
	subs	r3, r3, #5
	.loc 5 137 0
	movs	r0, #1
	.loc 5 136 0
	strh	r3, [r2, r5, lsl #1]	@ movhi
	.loc 5 137 0
	bl	FtlEctTblFlush
.LVL1259:
.L1455:
	.loc 5 139 0
	ldr	r3, [r4, #3960]
	cbnz	r3, .L1456
	.loc 5 140 0
	ldr	r3, [r4, #2708]
	.loc 5 141 0
	ldr	r0, [sp, #4]
	.loc 5 140 0
	adds	r3, r3, #1
	.loc 5 141 0
	ubfx	r0, r0, #10, #16
	.loc 5 140 0
	str	r3, [r4, #2708]
	.loc 5 141 0
	bl	FtlBbmMapBadBlock
.LVL1260:
	.loc 5 142 0
	bl	FtlBbmTblFlush
.LVL1261:
.L1456:
	.loc 5 144 0
	movs	r3, #0
	str	r3, [r4, #3960]
.L1467:
	.loc 5 149 0
	movs	r0, #1
	b	.L1447
.LVL1262:
.L1452:
	.loc 5 124 0
	movs	r2, #0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 125 0
	ldrh	r3, [r4, #2320]
	cmp	r3, #17
	bhi	.L1453
	b	.L1451
.LVL1263:
.L1454:
	.loc 5 148 0
	ldr	r3, .L1473+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1467
	b	.L1458
.LVL1264:
.L1471:
	.loc 5 160 0
	bl	FtlCacheWriteBack
.LVL1265:
	.loc 5 162 0
	ldrb	r1, [r4, #2427]	@ zero_extendqisi2
	ldrh	r3, [r4, #78]
	ldrh	r2, [r4, #3176]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1459
	.loc 5 162 0 is_stmt 0 discriminator 1
	movs	r2, #162
	ldr	r1, .L1473+12
	ldr	r0, .L1473+16
	bl	printk
.LVL1266:
.L1459:
	.loc 5 163 0 is_stmt 1
	ldrb	r3, [r4, #2427]	@ zero_extendqisi2
	.loc 5 165 0
	movs	r6, #0
	.loc 5 163 0
	ldrh	r0, [r4, #78]
	.loc 5 166 0
	mov	r9, #12
	.loc 5 163 0
	ldr	r2, [r4, #2300]
	ldrh	r1, [r4, #2420]
	.loc 5 168 0
	ldr	r10, .L1473+12
	.loc 5 163 0
	smulbb	r3, r3, r0
	.loc 5 168 0
	ldr	fp, .L1473+16
	.loc 5 163 0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 5 164 0
	ldr	r2, [r4, #2504]
	ldrh	r3, [r4, #3176]
	add	r3, r3, r2
	str	r3, [r4, #2504]
.LVL1267:
.L1460:
	.loc 5 165 0 discriminator 1
	ldrh	r2, [r4, #3176]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1464
	.loc 5 181 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1268:
	.loc 5 182 0
	ldrh	r0, [r4, #2420]
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1465
	.loc 5 183 0
	bl	INSERT_DATA_LIST
.LVL1269:
.L1466:
	.loc 5 187 0
	movs	r5, #0
.LVL1270:
	.loc 5 186 0
	movw	r6, #65535
	strh	r6, [r4, #2420]	@ movhi
	.loc 5 187 0
	strh	r5, [r4, #3176]	@ movhi
	.loc 5 188 0
	strh	r5, [r4, #3168]	@ movhi
	.loc 5 189 0
	bl	l2p_flush
.LVL1271:
	.loc 5 190 0
	bl	FtlVpcTblFlush
.LVL1272:
	.loc 5 191 0
	ldrh	r3, [r4, #2548]
	ldrh	r2, [r4, #2320]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L1449
	.loc 5 193 0
	movs	r3, #20
	.loc 5 192 0
	strh	r6, [r4, #2564]	@ movhi
	.loc 5 193 0
	strh	r3, [r4, #3124]	@ movhi
	b	.L1449
.LVL1273:
.L1464:
	.loc 5 166 0
	uxth	r7, r6
	ldr	r8, [r4, #3172]
	.loc 5 168 0
	ldr	r3, [r4, #2480]
	.loc 5 166 0
	mul	r7, r9, r7
	add	r5, r8, r7
	.loc 5 168 0
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1461
	.loc 5 168 0 is_stmt 0 discriminator 1
	movs	r2, #168
	mov	r1, r10
	mov	r0, fp
	bl	printk
.LVL1274:
.L1461:
	.loc 5 169 0 is_stmt 1
	movs	r2, #0
	add	r1, sp, #4
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1275:
	.loc 5 170 0
	ldr	r3, [sp, #4]
	ldr	r0, [r8, r7]
	cmp	r0, r3
	bne	.L1462
	.loc 5 171 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1276:
	.loc 5 172 0
	movs	r2, #1
	.loc 5 171 0
	mov	r7, r0
	.loc 5 172 0
	adds	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1277:
	.loc 5 173 0
	mov	r0, r7
.L1472:
	.loc 5 177 0
	bl	decrement_vpc_count
.LVL1278:
.L1463:
	adds	r6, r6, #1
	b	.L1460
.L1462:
	.loc 5 175 0
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1463
	.loc 5 177 0
	ldrh	r0, [r4, #2420]
	b	.L1472
.L1465:
	.loc 5 185 0
	bl	INSERT_FREE_LIST
.LVL1279:
	b	.L1466
.L1474:
	.align	2
.L1473:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2420
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
.LCFI179:
	.loc 5 290 0
	ldr	r4, .L1477
	ldrh	r1, [r4, #78]
	addw	r0, r4, #2420
	bl	FtlGcScanTempBlk
.LVL1280:
	.loc 5 291 0
	ldrh	r2, [r4, #2422]
	ldrh	r3, [r4, #78]
	cmp	r2, r3
	bcc	.L1475
.LBB515:
.LBB516:
	.loc 5 293 0
	addw	r0, r4, #3864
	bl	FtlMapBlkWriteDump_data
.LVL1281:
	.loc 5 294 0
	movs	r0, #0
	bl	FtlGcFreeTempBlock
.LVL1282:
	.loc 5 295 0
	movs	r3, #0
	str	r3, [r4, #3960]
.L1475:
	pop	{r4, pc}
.L1478:
	.align	2
.L1477:
	.word	.LANCHOR0
.LBE516:
.LBE515:
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
	.loc 2 2081 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI180:
	.loc 2 2082 0
	movs	r5, #0
	ldr	r4, .L1480
	.loc 2 2084 0
	addw	r6, r4, #2324
	.loc 2 2082 0
	str	r5, [r4, #3972]
	.loc 2 2084 0
	mov	r0, r6
	.loc 2 2086 0
	addw	r4, r4, #2372
	.loc 2 2084 0
	bl	FtlRecoverySuperblock
.LVL1283:
	.loc 2 2085 0
	mov	r0, r6
	bl	FtlSlcSuperblockCheck
.LVL1284:
	.loc 2 2086 0
	mov	r0, r4
	bl	FtlRecoverySuperblock
.LVL1285:
	.loc 2 2087 0
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
.LVL1286:
	.loc 2 2088 0
	bl	FtlGcPageRecovery
.LVL1287:
	.loc 2 2089 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1288:
	.loc 2 2091 0
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1481:
	.align	2
.L1480:
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
.LCFI181:
	.loc 5 320 0
	movs	r3, #0
	ldr	r4, .L1489
	.loc 5 321 0
	movs	r5, #0
	.loc 5 322 0
	movs	r6, #20
	.loc 5 320 0
	mov	r2, r3
	ldr	r1, [r4, #3140]
	ldr	r0, [r4, #3796]
	bl	FlashProgPages
.LVL1289:
.L1483:
	.loc 5 321 0 discriminator 1
	ldr	r1, [r4, #3140]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1486
	.loc 5 335 0
	ldr	r0, [r4, #3796]
	bl	FtlGcBufFree
.LVL1290:
	.loc 5 337 0
	ldrh	r3, [r4, #2424]
	.loc 5 336 0
	movs	r0, #0
	str	r0, [r4, #3140]
	.loc 5 337 0
	cbnz	r3, .L1482
	.loc 5 338 0
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1291:
	b	.L1488
.L1486:
	.loc 5 322 0
	muls	r3, r6, r3
	ldr	r2, [r4, #3796]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	adds	r0, r2, #1
	bne	.L1484
	.loc 5 323 0
	ldrh	r0, [r4, #2420]
	movs	r5, #0
	ldr	r1, [r4, #2300]
	strh	r5, [r1, r0, lsl #1]	@ movhi
	.loc 5 324 0
	strh	r2, [r4, #2420]	@ movhi
	.loc 5 325 0
	ldr	r2, [r4, #2708]
	adds	r2, r2, #1
	str	r2, [r4, #2708]
	.loc 5 326 0
	ldr	r2, [r4, #3796]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL1292:
	.loc 5 327 0
	bl	FtlBbmTblFlush
.LVL1293:
	.loc 5 328 0
	bl	FtlGcPageVarInit
.LVL1294:
.L1488:
	.loc 5 339 0
	movs	r0, #1
.L1482:
	.loc 5 342 0
	pop	{r4, r5, r6, pc}
.L1484:
	.loc 5 331 0 discriminator 2
	ldr	r3, [r1, #12]
.LVL1295:
	.loc 5 332 0 discriminator 2
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL1296:
	b	.L1483
.L1490:
	.align	2
.L1489:
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
.LCFI182:
	.loc 5 301 0
	movw	r3, #65535
	ldr	r4, .L1494
	ldrh	r2, [r4, #2420]
	cmp	r2, r3
	beq	.L1492
	.loc 5 301 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #2424]
	cbnz	r3, .L1493
.L1492:
	.loc 5 302 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1297:
	.loc 5 303 0
	movs	r0, #0
	.loc 5 304 0
	movs	r5, #0
	.loc 5 303 0
	bl	FtlGcFreeTempBlock
.LVL1298:
	.loc 5 305 0
	ldr	r0, .L1494+4
	.loc 5 304 0
	strb	r5, [r4, #2428]
	.loc 5 305 0
	bl	allocate_data_superblock
.LVL1299:
	.loc 5 306 0
	strh	r5, [r4, #3168]	@ movhi
	.loc 5 307 0
	strh	r5, [r4, #3176]	@ movhi
	.loc 5 308 0
	bl	l2p_flush
.LVL1300:
	.loc 5 309 0
	mov	r0, r5
	bl	FtlEctTblFlush
.LVL1301:
	.loc 5 310 0
	bl	FtlVpcTblFlush
.LVL1302:
.L1493:
	.loc 5 312 0
	ldr	r0, .L1494+4
	.loc 5 313 0
	pop	{r3, r4, r5, lr}
.LCFI183:
	.loc 5 312 0
	b	get_new_active_ppa
.LVL1303:
.L1495:
	.align	2
.L1494:
	.word	.LANCHOR0
	.word	.LANCHOR0+2420
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
.LVL1304:
	.loc 5 478 0
	ldr	r3, .L1615
	.loc 5 470 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI184:
	mov	r7, r0
	.pad #36
	sub	sp, sp, #36
.LCFI185:
	mov	r4, r3
	.loc 5 478 0
	ldr	r0, [r3, #3780]
.LVL1305:
	cmp	r0, #0
	bne	.L1571
	.loc 5 481 0
	ldrh	r2, [r3, #2312]
	cmp	r2, #47
	bls	.L1496
	.loc 5 485 0
	ldr	r2, .L1615+4
	ldrh	r1, [r2, #4]
.LVL1306:
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1498
.L1501:
.LBB520:
.LBB521:
	.loc 5 489 0
	ldrh	r2, [r4, #3180]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1499
.L1500:
	.loc 5 496 0
	ldr	r3, [r4, #3128]
	.loc 5 497 0
	movw	r5, #65535
	ldrh	r1, [r4, #2564]
	.loc 5 496 0
	adds	r3, r3, #1
	add	r3, r3, r7, lsl #7
	.loc 5 497 0
	cmp	r1, r5
	.loc 5 496 0
	str	r3, [r4, #3128]
	.loc 5 497 0
	bne	.L1502
	ldrh	r2, [r4, #2420]
	cmp	r2, r1
	bne	.L1503
	ldrh	r8, [r4, #3178]
	cmp	r8, r2
	bne	.L1504
	.loc 5 498 0
	ldrh	r2, [r4, #2320]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1504
.LBB522:
	.loc 5 502 0
	ldr	r3, .L1615+8
	.loc 5 501 0
	movs	r6, #0
	str	r6, [r4, #3128]
	.loc 5 502 0
	strh	r6, [r3, #-228]	@ movhi
	.loc 5 503 0
	bl	GetSwlReplaceBlock
.LVL1307:
	.loc 5 504 0
	cmp	r0, r8
	.loc 5 503 0
	mov	r5, r0
	.loc 5 504 0
	bne	.L1575
	.loc 5 505 0
	ldrh	r2, [r4, #2320]
	ldrh	r3, [r4, #3126]
	cmp	r2, r3
	bcs	.L1507
	.loc 5 506 0
	movs	r0, #64
	bl	List_get_gc_head_node
.LVL1308:
	.loc 5 507 0
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1509
	.loc 5 508 0
	mov	r0, r3
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1510
	.loc 5 509 0
	mov	r0, r6
	bl	List_get_gc_head_node
.LVL1309:
	uxth	r6, r0
	.loc 5 510 0
	movs	r3, #128
	strh	r3, [r4, #3126]	@ movhi
	.loc 5 520 0
	cmp	r6, r5
	beq	.L1509
.L1506:
	.loc 5 521 0
	ldr	r3, [r4, #2300]
	mov	r5, r6
	ldrh	r1, [r4, #3124]
	ldrh	r2, [r4, #2320]
	ldrh	r3, [r3, r6, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2304]
	ldr	r0, .L1615+12
	ldrh	r1, [r1, r6, lsl #1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
.LVL1310:
	b	.L1509
.L1498:
.LBE522:
.LBE521:
.LBE520:
	.loc 5 485 0 discriminator 1
	ldrh	r3, [r3, #2420]
	cmp	r3, r2
	beq	.L1501
	.loc 5 486 0
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1311:
	cmp	r0, #0
	beq	.L1501
	.loc 5 487 0
	movs	r0, #1
	b	.L1496
.L1499:
.LBB528:
.LBB525:
	.loc 5 489 0
	ldrh	r3, [r4, #3178]
	cmp	r3, r1
	.loc 5 490 0
	itt	eq
	strheq	r2, [r4, #3178]	@ movhi
	.loc 5 491 0
	strheq	r3, [r4, #3180]	@ movhi
	b	.L1500
.L1510:
.LBB523:
	.loc 5 512 0
	movs	r3, #64
.L1608:
	.loc 5 516 0
	strh	r3, [r4, #3126]	@ movhi
.L1509:
	.loc 5 523 0
	bl	FtlGcReFreshBadBlk
.LVL1312:
.LBE523:
	.loc 5 527 0
	cmp	r7, #0
	bne	.L1512
	movw	r3, #65535
	cmp	r5, r3
	bne	.L1512
.L1569:
	.loc 5 528 0
	ldrh	r3, [r4, #2320]
	cmp	r3, #24
	bhi	.L1576
	.loc 5 530 0
	cmp	r3, #16
	ldrh	r6, [r4, #78]
	bls	.L1514
	.loc 5 531 0
	lsrs	r6, r6, #5
.LVL1313:
.L1513:
	.loc 5 539 0
	ldrh	r2, [r4, #3124]
	cmp	r2, r3
	bcs	.L1517
	.loc 5 540 0
	ldrh	r3, [r4, #2420]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1518
	ldrh	r2, [r4, #3178]
	cmp	r2, r3
	bne	.L1518
	.loc 5 542 0
	ldr	r3, .L1615+8
	ldrh	r0, [r3, #-228]
	cbnz	r0, .L1519
	ldr	r3, [r4, #2480]
	ldr	r2, [r4, #2476]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1520
.L1519:
	.loc 5 543 0
	ldrh	r3, [r4, #2548]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1609:
	.loc 5 545 0
	strh	r3, [r4, #3124]	@ movhi
	.loc 5 546 0
	movs	r3, #0
	str	r3, [r4, #3136]
.LVL1314:
.L1496:
.LBE525:
.LBE528:
	.loc 5 754 0
	add	sp, sp, #36
.LCFI186:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1315:
.L1507:
.LCFI187:
.LBB529:
.LBB526:
.LBB524:
	.loc 5 516 0
	movs	r3, #80
	b	.L1608
.L1575:
	.loc 5 504 0
	mov	r6, r0
	b	.L1506
.L1514:
.LBE524:
	.loc 5 532 0
	cmp	r3, #12
	bls	.L1515
	.loc 5 533 0
	lsrs	r6, r6, #4
.LVL1316:
	b	.L1513
.LVL1317:
.L1515:
	.loc 5 534 0
	cmp	r3, #8
	bls	.L1513
	.loc 5 535 0
	lsrs	r6, r6, #2
.LVL1318:
	b	.L1513
.LVL1319:
.L1576:
	.loc 5 529 0
	movs	r6, #1
.LVL1320:
	b	.L1513
.L1520:
	.loc 5 545 0
	movs	r3, #18
	b	.L1609
.L1518:
	.loc 5 549 0
	ldrh	r3, [r4, #2548]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3124]	@ movhi
.L1517:
	.loc 5 551 0
	ldrh	r3, [r4, #3182]
	cbz	r3, .L1577
	.loc 5 552 0
	adds	r6, r6, #32
.LVL1321:
	uxth	r6, r6
.LVL1322:
.L1577:
	.loc 5 551 0
	movw	r5, #65535
.L1523:
	.loc 5 595 0
	ldrh	r3, [r4, #2564]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1532
	.loc 5 596 0
	cmp	r5, r3
	beq	.L1533
	.loc 5 597 0
	strh	r5, [r4, #2564]	@ movhi
.L1534:
	.loc 5 607 0
	ldrh	r0, [r4, #2564]
	movw	r7, #65535
.LVL1323:
	.loc 5 606 0
	movs	r3, #0
	strb	r3, [r4, #2572]
	.loc 5 607 0
	cmp	r0, r7
	beq	.L1532
	.loc 5 608 0
	bl	IsBlkInGcList
.LVL1324:
	cbz	r0, .L1537
	.loc 5 609 0
	strh	r7, [r4, #2564]	@ movhi
.L1537:
	.loc 5 611 0
	ldrh	r2, [r4, #2564]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1532
	.loc 5 612 0
	ldr	r0, .L1615+16
	bl	make_superblock
.LVL1325:
	.loc 5 615 0
	ldr	r2, .L1615+8
	.loc 5 613 0
	movs	r3, #0
	strh	r3, [r4, #2566]	@ movhi
	.loc 5 614 0
	strb	r3, [r4, #2570]
	.loc 5 615 0
	strh	r3, [r2, #-226]	@ movhi
	.loc 5 616 0
	ldrh	r1, [r4, #2564]
	ldr	r3, [r4, #2300]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-224]	@ movhi
.LVL1326:
.L1532:
	.loc 5 621 0
	ldrh	r3, [r4, #2564]
	ldrh	r2, [r4, #2324]
	cmp	r2, r3
	beq	.L1538
	ldrh	r2, [r4, #2372]
	cmp	r2, r3
	beq	.L1538
	.loc 5 662 0
	ldr	r7, .L1615+8
.LVL1327:
.L1539:
	.loc 5 627 0
	ldrh	r9, [r4, #2564]
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1540
	.loc 5 628 0
	movs	r3, #0
	.loc 5 643 0
	mov	r10, r3
.LVL1328:
	.loc 5 628 0
	str	r3, [r4, #3136]
.L1541:
	.loc 5 631 0
	ldrh	r8, [r4, #3132]
.LVL1329:
	mov	r0, r8
	bl	List_get_gc_head_node
.LVL1330:
	uxth	fp, r0
	.loc 5 632 0
	cmp	fp, r9
	.loc 5 631 0
	strh	fp, [r4, #2564]	@ movhi
	.loc 5 632 0
	bne	.L1542
	.loc 5 633 0
	movs	r3, #0
	.loc 5 634 0
	movs	r0, #8
	.loc 5 633 0
	strh	r3, [r4, #3132]	@ movhi
	b	.L1496
.LVL1331:
.L1524:
	.loc 5 558 0
	movs	r3, #0
	.loc 5 560 0
	ldrh	r7, [r4, #2320]
.LVL1332:
	.loc 5 558 0
	str	r3, [r4, #3136]
	.loc 5 560 0
	ldrh	r3, [r4, #3124]
	ldr	r5, .L1615+8
	cmp	r3, r7
	bcs	.L1525
	.loc 5 561 0
	ldrh	r3, [r5, #-228]
	cbnz	r3, .L1526
	ldr	r3, [r4, #2480]
	ldr	r2, [r4, #2476]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1527
.L1526:
	.loc 5 562 0
	ldrh	r3, [r4, #2548]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1610:
	.loc 5 564 0
	strh	r3, [r4, #3124]	@ movhi
.L1612:
	.loc 5 578 0
	bl	FtlReadRefresh
.LVL1333:
	.loc 5 579 0
	ldrh	r0, [r5, #-228]
	b	.L1496
.L1527:
	.loc 5 564 0
	movs	r3, #18
	b	.L1610
.L1525:
	.loc 5 572 0
	ldrh	r0, [r5, #-228]
	cmp	r0, #0
	bne	.L1529
	.loc 5 573 0
	ldrh	r6, [r4, #2548]
	add	r3, r6, r6, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3124]	@ movhi
	.loc 5 574 0
	bl	List_get_gc_head_node
.LVL1334:
	.loc 5 575 0
	ldr	r3, [r4, #2300]
	uxth	r0, r0
	ldrh	r1, [r4, #80]
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r4, #8]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1530
	.loc 5 576 0
	subs	r6, r6, #1
	.loc 5 575 0
	cmp	r7, r6
	bge	.L1612
.L1530:
	.loc 5 584 0
	cmp	r2, #0
	bne	.L1529
	.loc 5 585 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1335:
	.loc 5 586 0
	ldrh	r0, [r4, #2320]
	adds	r0, r0, #1
	b	.L1496
.LVL1336:
.L1533:
	.loc 5 598 0
	ldrh	r3, [r4, #3178]
	cmp	r3, r5
	beq	.L1534
	.loc 5 600 0
	ldr	r2, [r4, #2300]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1535
	.loc 5 601 0
	strh	r5, [r4, #3178]	@ movhi
.L1535:
	.loc 5 602 0
	ldrh	r3, [r4, #3178]
	strh	r3, [r4, #2564]	@ movhi
	.loc 5 603 0
	movw	r3, #65535
	strh	r3, [r4, #3178]	@ movhi
	b	.L1534
.L1616:
	.align	2
.L1615:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC103
	.word	.LANCHOR0+2564
.L1538:
	.loc 5 622 0
	movw	r3, #65535
	strh	r3, [r4, #2564]	@ movhi
.LVL1337:
.L1613:
	.loc 5 648 0
	ldr	r3, .L1617
	ldrh	r0, [r3, #-228]
	b	.L1496
.LVL1338:
.L1542:
	str	r0, [sp, #8]
	.loc 5 636 0
	mov	r0, fp
	bl	IsBlkInGcList
.LVL1339:
	add	r8, r8, #1
	ldr	r3, [sp, #8]
	cbz	r0, .L1543
	.loc 5 637 0
	strh	r8, [r4, #3132]	@ movhi
	b	.L1541
.L1543:
	.loc 5 641 0
	uxth	r0, r3
	ldrh	ip, [r4, #8]
	ldrh	r3, [r4, #78]
	.loc 5 640 0
	uxth	r8, r8
	.loc 5 641 0
	ldr	r2, [r4, #2300]
	.loc 5 640 0
	strh	r8, [r4, #3132]	@ movhi
	.loc 5 641 0
	mul	r3, ip, r3
	ldrh	r1, [r2, r0, lsl #1]
	add	ip, r3, r3, lsr #31
	cmp	r1, ip, asr #1
	bgt	.L1545
	.loc 5 642 0
	cmp	r8, #48
	bls	.L1546
	cmp	r1, #8
	bls	.L1546
	ldrh	r1, [r4, #3168]
	cmp	r1, #35
	bhi	.L1546
.L1545:
	.loc 5 643 0
	strh	r10, [r4, #3132]	@ movhi
.L1546:
	.loc 5 645 0
	ldrh	r2, [r2, r0, lsl #1]
	cmp	r3, r2
	bgt	.L1547
	cmp	r5, r9
	bne	.L1547
	.loc 5 647 0
	movs	r3, #0
	.loc 5 646 0
	strh	r5, [r4, #2564]	@ movhi
	.loc 5 647 0
	strh	r3, [r4, #3132]	@ movhi
	b	.L1613
.L1547:
	.loc 5 651 0
	cbnz	r2, .L1548
	.loc 5 652 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1340:
	.loc 5 653 0
	ldrh	r3, [r4, #3132]
	adds	r3, r3, #1
	strh	r3, [r4, #3132]	@ movhi
	b	.L1541
.L1548:
	.loc 5 656 0
	movs	r3, #0
	strb	r3, [r4, #2572]
	.loc 5 658 0
	ldrh	r3, [r4, #2324]
	cmp	r3, fp
	bne	.L1549
	movw	r2, #658
	ldr	r1, .L1617+4
	ldr	r0, .L1617+8
	bl	printk
.LVL1341:
.L1549:
	.loc 5 659 0
	ldrh	r2, [r4, #2564]
	ldrh	r3, [r4, #2372]
	cmp	r2, r3
	bne	.L1550
	movw	r2, #659
	ldr	r1, .L1617+4
	ldr	r0, .L1617+8
	bl	printk
.LVL1342:
.L1550:
	.loc 5 660 0
	ldrh	r2, [r4, #2564]
	ldrh	r3, [r4, #2420]
	cmp	r2, r3
	bne	.L1551
	mov	r2, #660
	ldr	r1, .L1617+4
	ldr	r0, .L1617+8
	bl	printk
.LVL1343:
.L1551:
	.loc 5 661 0
	ldr	r0, .L1617+12
	bl	make_superblock
.LVL1344:
	.loc 5 663 0
	ldrh	r1, [r4, #2564]
	.loc 5 662 0
	movs	r3, #0
	.loc 5 663 0
	ldr	r2, [r4, #2300]
	.loc 5 662 0
	strh	r3, [r7, #-226]	@ movhi
	.loc 5 663 0
	ldrh	r2, [r2, r1, lsl #1]
	.loc 5 664 0
	strh	r3, [r4, #2566]	@ movhi
	.loc 5 665 0
	strb	r3, [r4, #2570]
	.loc 5 663 0
	strh	r2, [r7, #-224]	@ movhi
.LVL1345:
.L1540:
	.loc 5 671 0
	bl	FtlReadRefresh
.LVL1346:
	.loc 5 674 0
	movs	r3, #1
	str	r3, [r4, #3780]
	.loc 5 675 0
	ldrh	r3, [r4, #78]
.LVL1347:
	str	r3, [sp, #12]
	.loc 5 676 0
	ldrh	r3, [r4, #2566]
.LVL1348:
	ldr	r1, [sp, #12]
.LVL1349:
	adds	r2, r3, r6
	cmp	r2, r1
	.loc 5 677 0
	itt	gt
	movgt	r2, r1
	subgt	r6, r2, r3
.LVL1350:
	.loc 5 689 0
	mov	r3, #0
	.loc 5 677 0
	it	gt
	uxthgt	r6, r6
.LVL1351:
.L1611:
	str	r3, [sp, #8]
	.loc 5 679 0
	ldrh	r3, [sp, #8]
	cmp	r6, r3
	bls	.L1561
	.loc 5 684 0
	ldr	r3, [sp, #8]
	.loc 5 683 0
	movw	r10, #65535
.LVL1352:
	.loc 5 684 0
	ldrh	r1, [r4, #2566]
	mov	fp, #20
	.loc 5 681 0
	ldrh	lr, [r4, #8]
	.loc 5 684 0
	ldr	r0, [r4, #3156]
	add	r1, r1, r3
	movs	r3, #0
	ldr	ip, .L1617+16
	mov	r8, r3
	b	.L1562
.LVL1353:
.L1555:
	.loc 5 682 0
	ldrh	r2, [ip, #2]!
.LVL1354:
	.loc 5 683 0
	cmp	r2, r10
	beq	.L1554
	.loc 5 684 0
	mla	r9, fp, r8, r0
	.loc 5 685 0
	add	r8, r8, #1
.LVL1355:
	.loc 5 684 0
	orr	r2, r1, r2, lsl #10
.LVL1356:
	.loc 5 685 0
	uxth	r8, r8
.LVL1357:
	.loc 5 684 0
	str	r2, [r9, #4]
.LVL1358:
.L1554:
	adds	r3, r3, #1
.LVL1359:
.L1562:
	.loc 5 681 0
	uxth	r2, r3
.LVL1360:
	cmp	lr, r2
	bhi	.L1555
	.loc 5 689 0
	mov	fp, #0
	ldrb	r2, [r4, #2572]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.LVL1361:
.L1556:
	.loc 5 691 0
	uxth	r3, fp
	cmp	r8, r3
	bhi	.L1560
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L1611
.L1560:
	mov	r9, #20
	.loc 5 692 0
	ldr	r3, [r4, #3156]
	mul	r9, r9, fp
	add	r2, r3, r9
	.loc 5 693 0
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	beq	.L1557
	.loc 5 692 0
	ldr	r10, [r2, #12]
	.loc 5 693 0
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1557
	.loc 5 695 0
	ldr	r3, [r10, #8]
.LVL1362:
	.loc 5 696 0
	adds	r2, r3, #1
	bne	.L1558
	mov	r2, #696
	ldr	r1, .L1617+4
	ldr	r0, .L1617+8
	str	r3, [sp, #16]
	bl	printk
.LVL1363:
	ldr	r3, [sp, #16]
.LVL1364:
.L1558:
	.loc 5 697 0
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #28
	bl	log2phys
.LVL1365:
	.loc 5 698 0
	ldr	r0, [r4, #3156]
	ldr	r3, [sp, #28]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1557
	.loc 5 700 0
	ldrh	r3, [r7, #-226]
	.loc 5 701 0
	movs	r2, #20
	ldr	r1, [r4, #3140]
	str	r2, [sp, #20]
	.loc 5 700 0
	adds	r3, r3, #1
	strh	r3, [r7, #-226]	@ movhi
	.loc 5 701 0
	ldr	r3, [r4, #3796]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
	str	r1, [r3, #16]
	str	r3, [sp, #16]
	.loc 5 702 0
	bl	Ftl_get_new_temp_ppa
.LVL1366:
	ldr	r3, [sp, #16]
	.loc 5 703 0
	ldr	r1, [r4, #3796]
	ldr	r2, [sp, #20]
	.loc 5 702 0
	str	r0, [r3, #4]
	.loc 5 703 0
	ldr	r3, [r4, #3140]
	mla	r3, r2, r3, r1
	ldr	r2, [r4, #3156]
	add	r2, r2, r9
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	.loc 5 712 0
	movs	r1, #1
	.loc 5 704 0
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	.loc 5 705 0
	ldr	r3, [sp, #28]
.LVL1367:
	str	r3, [r10, #12]
	.loc 5 706 0
	ldrh	r3, [r4, #2420]
.LVL1368:
	strh	r3, [r10, #2]	@ movhi
	.loc 5 709 0
	ldr	r3, [r4, #2520]
	.loc 5 712 0
	ldr	r0, [r4, #3156]
	.loc 5 709 0
	str	r3, [r10, #4]
	.loc 5 711 0
	ldr	r3, [r4, #3140]
	.loc 5 712 0
	add	r0, r0, r9
	.loc 5 711 0
	adds	r3, r3, #1
	str	r3, [r4, #3140]
	.loc 5 712 0
	bl	FtlGcBufAlloc
.LVL1369:
	.loc 5 713 0
	ldrb	r2, [r4, #2427]	@ zero_extendqisi2
	ldr	r3, [r4, #3140]
	cmp	r2, r3
	beq	.L1559
	ldrh	r3, [r4, #2424]
	cbnz	r3, .L1557
.L1559:
	.loc 5 714 0
	bl	Ftl_gc_temp_data_write_back
.LVL1370:
	cbz	r0, .L1557
.L1614:
	.loc 5 729 0
	movs	r3, #0
	.loc 5 730 0
	ldrh	r0, [r7, #-228]
	.loc 5 729 0
	str	r3, [r4, #3780]
	b	.L1496
.L1557:
	add	fp, fp, #1
	b	.L1556
.LVL1371:
.L1561:
	.loc 5 723 0
	ldrh	r3, [r4, #2566]
	add	r6, r6, r3
.LVL1372:
	.loc 5 725 0
	ldr	r3, [sp, #12]
.LVL1373:
	.loc 5 723 0
	uxth	r6, r6
	.loc 5 725 0
	cmp	r3, r6
	.loc 5 723 0
	strh	r6, [r4, #2566]	@ movhi
	.loc 5 725 0
	bls	.L1563
	ldrh	r2, [r7, #-226]
	ldrh	r3, [r7, #-224]
.LVL1374:
	cmp	r2, r3
	bne	.L1564
.LVL1375:
.L1563:
	.loc 5 727 0
	ldr	r3, [r4, #3140]
.LVL1376:
	cbz	r3, .L1565
	.loc 5 728 0
	bl	Ftl_gc_temp_data_write_back
.LVL1377:
	cmp	r0, #0
	bne	.L1614
.L1565:
	.loc 5 733 0
	ldrh	r1, [r7, #-226]
	cbnz	r1, .L1566
	ldrh	r2, [r4, #2564]
	ldr	r3, [r4, #2300]
	ldrh	r0, [r3, r2, lsl #1]
	cbz	r0, .L1566
	.loc 5 735 0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	.loc 5 736 0
	ldrh	r0, [r4, #2564]
	bl	update_vpc_list
.LVL1378:
	.loc 5 737 0
	bl	FtlCacheWriteBack
.LVL1379:
	.loc 5 738 0
	bl	l2p_flush
.LVL1380:
	.loc 5 739 0
	bl	FtlVpcTblFlush
.LVL1381:
.L1566:
	.loc 5 741 0
	movw	r3, #65535
	strh	r3, [r4, #2564]	@ movhi
.L1564:
	.loc 5 743 0
	ldrh	r3, [r4, #2320]
	cmp	r3, #2
	bhi	.L1567
	.loc 5 745 0
	ldrh	r6, [r4, #78]
.LVL1382:
	b	.L1539
.LVL1383:
.L1567:
	.loc 5 750 0
	ldrh	r0, [r7, #-228]
	.loc 5 748 0
	movs	r2, #0
	str	r2, [r4, #3780]
	.loc 5 750 0
	cmp	r0, #0
	bne	.L1496
	.loc 5 753 0
	adds	r0, r3, #1
	b	.L1496
.LVL1384:
.L1571:
.LBE526:
.LBE529:
	.loc 5 479 0
	movs	r0, #0
	b	.L1496
.L1618:
	.align	2
.L1617:
	.word	.LANCHOR3
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2564
	.word	.LANCHOR0+2578
.LVL1385:
.L1502:
.LBB530:
.LBB527:
	.loc 5 527 0
	cmp	r7, #0
	beq	.L1569
.L1512:
	.loc 5 557 0
	ldrh	r2, [r4, #2420]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1580
.L1570:
	ldrh	r3, [r4, #3178]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1580
	cmp	r5, r3
	bne	.L1580
	ldrh	r3, [r4, #2564]
	cmp	r3, r5
	beq	.L1524
.L1529:
	.loc 5 556 0
	movw	r5, #65535
.L1580:
	movs	r6, #1
.LVL1386:
	b	.L1523
.LVL1387:
.L1504:
	.loc 5 527 0
	cmp	r7, #0
	beq	.L1569
	movw	r5, #65535
	b	.L1570
.L1503:
	cmp	r7, #0
	bne	.L1529
	b	.L1569
.LBE527:
.LBE530:
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
	.loc 5 757 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.loc 5 758 0
	movs	r1, #1
	mov	r0, r1
	b	rk_ftl_garbage_collect
.LVL1388:
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
	.loc 3 814 0
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1389:
	.loc 3 824 0
	cmp	r0, #16
	.loc 3 814 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI188:
	.pad #60
	sub	sp, sp, #60
.LCFI189:
	.loc 3 814 0
	mov	r7, r1
	mov	r10, r3
	str	r2, [sp, #28]
	.loc 3 824 0
	bne	.L1621
	.loc 3 825 0
	mov	r2, r3
.LVL1390:
	ldr	r1, [sp, #28]
	add	r0, r7, #256
.LVL1391:
	bl	FtlVendorPartRead
.LVL1392:
	mov	r8, r0
.LVL1393:
.L1620:
	.loc 3 901 0
	mov	r0, r8
.LVL1394:
	add	sp, sp, #60
.LCFI190:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1395:
.L1621:
.LCFI191:
	.loc 3 830 0
	ldr	r3, [sp, #28]
.LVL1396:
	ldr	r4, .L1652
	adds	r3, r1, r3
.LVL1397:
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #116]
	cmp	r2, r3
	bhi	.L1641
	.loc 3 830 0 is_stmt 0 discriminator 1
	ldr	r3, .L1652+4
	ldr	r3, [r3]
	adds	r5, r3, #1
	beq	.L1642
	.loc 3 833 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1398:
	.loc 3 835 0
	ldrh	r5, [r4, #34]
	mov	r0, r7
	.loc 3 828 0
	mov	r9, #0
.LVL1399:
	.loc 3 840 0
	mov	r6, r9
.LVL1400:
	.loc 3 815 0
	mov	r8, r9
	.loc 3 835 0
	mov	r1, r5
	bl	__aeabi_uidiv
.LVL1401:
	.loc 3 836 0
	ldr	r3, [sp, #8]
	mov	r1, r5
	.loc 3 835 0
	str	r0, [sp, #12]
	.loc 3 836 0
	subs	r0, r3, #1
	bl	__aeabi_uidiv
.LVL1402:
	.loc 3 837 0
	ldr	r3, [sp, #12]
.LVL1403:
	.loc 3 835 0
	ldr	r5, [sp, #12]
.LVL1404:
	.loc 3 836 0
	str	r0, [sp, #16]
	.loc 3 837 0
	rsb	r3, r3, #1
.LVL1405:
	.loc 3 828 0
	str	r9, [sp, #24]
.LVL1406:
	.loc 3 837 0
	add	r3, r3, r0
.LVL1407:
	.loc 3 820 0
	str	r9, [sp, #20]
	.loc 3 837 0
	str	r3, [sp, #4]
	.loc 3 841 0
	ldr	r2, [sp, #4]
.LVL1408:
	ldr	r3, [r4, #2484]
	add	r3, r3, r2
	str	r3, [r4, #2484]
.LVL1409:
.L1623:
	.loc 3 842 0
	ldr	r3, [sp, #4]
.LVL1410:
	cbnz	r3, .L1639
	.loc 3 898 0
	ldr	r3, .L1652
.LVL1411:
	ldrh	r2, [r3, #3182]
	cbnz	r2, .L1640
	.loc 3 898 0 is_stmt 0 discriminator 1
	ldrh	r3, [r3, #2320]
	cmp	r3, #31
	bhi	.L1620
.L1640:
	.loc 3 899 0 is_stmt 1
	movs	r1, #1
	movs	r0, #0
.LVL1412:
	bl	rk_ftl_garbage_collect
.LVL1413:
	b	.L1620
.LVL1414:
.L1639:
	.loc 3 843 0
	movs	r2, #0
	add	r1, sp, #52
	mov	r0, r5
.LVL1415:
	bl	log2phys
.LVL1416:
	.loc 3 844 0
	ldr	r2, [sp, #52]
	adds	r0, r2, #1
	bne	.L1624
	mov	fp, #0
.LVL1417:
.L1625:
	.loc 3 845 0 discriminator 1
	ldrh	r0, [r4, #34]
.LVL1418:
	cmp	fp, r0
	bcc	.L1627
.L1628:
	.loc 3 874 0
	ldr	r3, [sp, #4]
.LVL1419:
	.loc 3 872 0
	adds	r5, r5, #1
	.loc 3 874 0
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1632
	.loc 3 874 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #8]
.LVL1420:
	cmp	r6, r3, lsl #2
	bne	.L1623
.LVL1421:
.L1632:
	.loc 3 875 0 is_stmt 1
	cmp	r6, #0
	beq	.L1623
	.loc 3 876 0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3792]
	.loc 3 877 0
	mov	fp, #0
.LVL1422:
	.loc 3 876 0
	bl	FlashReadPages
.LVL1423:
	.loc 3 883 0
	lsl	r3, r9, #9
	str	r3, [sp, #40]
	.loc 3 880 0
	ldr	r3, [sp, #20]
.LVL1424:
	lsls	r3, r3, #9
.LVL1425:
	str	r3, [sp, #32]
	ldr	r3, [sp, #24]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
.L1638:
	movs	r3, #20
	.loc 3 878 0
	ldr	r2, [r4, #3792]
	mul	r3, r3, fp
	ldr	r0, [sp, #12]
.LVL1426:
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1634
	.loc 3 879 0
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3776]
	cmp	r1, r2
	bne	.L1635
	str	r3, [sp, #44]
	.loc 3 880 0
	mov	r0, r10
.LVL1427:
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.LVL1428:
.L1651:
	.loc 3 883 0
	bl	ftl_memcpy
.LVL1429:
	ldr	r3, [sp, #44]
.LVL1430:
.L1635:
	.loc 3 885 0
	ldr	r2, [r4, #3792]
	ldr	r0, [r2, r3]
.LVL1431:
	add	ip, r2, r3
	adds	r1, r0, #1
	.loc 3 886 0
	itttt	eq
	ldreq	r1, [r4, #2684]
	.loc 3 887 0
	moveq	r8, r0
	.loc 3 886 0
	addeq	r1, r1, #1
	streq	r1, [r4, #2684]
	.loc 3 889 0
	ldr	r3, [r2, r3]
	cmp	r3, #256
	bne	.L1637
.LBB537:
	.loc 3 890 0
	ldr	r0, [ip, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1432:
	.loc 3 891 0
	bl	FtlGcRefreshBlock
.LVL1433:
.L1637:
.LBE537:
	.loc 3 877 0 discriminator 2
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1638
	.loc 3 894 0
	movs	r6, #0
	b	.L1623
.LVL1434:
.L1627:
	.loc 3 846 0
	mla	r0, r0, r5, fp
.LVL1435:
	.loc 3 847 0
	cmp	r7, r0
	bhi	.L1626
	.loc 3 847 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1626
	.loc 3 848 0 is_stmt 1
	subs	r0, r0, r7
.LVL1436:
.LBB538:
.LBB539:
.LBB540:
	.loc 1 202 0
	mov	r1, #512
	add	r0, r10, r0, lsl #9
	bl	__memzero
.LVL1437:
.L1626:
.LBE540:
.LBE539:
.LBE538:
	.loc 3 845 0 discriminator 2
	add	fp, fp, #1
	b	.L1625
.LVL1438:
.L1624:
	.loc 3 851 0
	ldr	r3, [r4, #3792]
	movs	r1, #20
	ldrh	fp, [r4, #34]
.LVL1439:
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	.loc 3 852 0
	ldr	r2, [sp, #12]
.LVL1440:
	cmp	r5, r2
	bne	.L1629
	.loc 3 853 0
	ldr	r2, [r4, #3776]
.LVL1441:
	.loc 3 854 0
	mov	r1, fp
	mov	r0, r7
	.loc 3 853 0
	str	r3, [sp, #32]
	str	r2, [r3, #8]
	.loc 3 854 0
	bl	__aeabi_uidivmod
.LVL1442:
	.loc 3 855 0
	sub	r2, fp, r1
	.loc 3 854 0
	str	r1, [sp, #20]
.LVL1443:
	ldr	r1, [sp, #28]
.LVL1444:
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	.loc 3 857 0
	cmp	r3, fp
	str	r3, [sp, #24]
	ldr	r3, [sp, #32]
	bne	.L1630
	.loc 3 858 0
	str	r10, [r3, #8]
.L1630:
	.loc 3 867 0
	ldrh	r2, [r4, #88]
	ldr	r1, [r4, #3772]
.LVL1445:
	.loc 3 868 0
	str	r5, [r3, #16]
	.loc 3 867 0
	muls	r2, r6, r2
	.loc 3 869 0
	adds	r6, r6, #1
	.loc 3 867 0
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1628
.LVL1446:
.L1629:
	.loc 3 859 0
	ldr	r2, [sp, #16]
.LVL1447:
	cmp	r5, r2
	bne	.L1631
	.loc 3 860 0
	ldr	r2, [r4, #3768]
.LVL1448:
	.loc 3 861 0
	ldr	r1, [sp, #8]
	.loc 3 860 0
	str	r2, [r3, #8]
	.loc 3 861 0
	mul	r2, fp, r5
	sub	r9, r1, r2
	.loc 3 862 0
	cmp	fp, r9
	bne	.L1630
.L1650:
	.loc 3 865 0
	subs	r2, r2, r7
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1630
.LVL1449:
.L1631:
	mul	r2, r5, fp
.LVL1450:
	b	.L1650
.LVL1451:
.L1634:
	.loc 3 881 0
	ldr	r0, [sp, #16]
.LVL1452:
	cmp	r0, r1
	bne	.L1635
	.loc 3 882 0
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3768]
	cmp	r1, r2
	bne	.L1635
	str	r3, [sp, #44]
	.loc 3 883 0
	ldrh	r0, [r4, #34]
.LVL1453:
	ldr	r3, [sp, #16]
.LVL1454:
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r10, r0, lsl #9
	b	.L1651
.LVL1455:
.L1641:
	.loc 3 831 0
	mov	r8, #-1
	b	.L1620
.L1642:
	mov	r8, r3
	b	.L1620
.L1653:
	.align	2
.L1652:
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
	.loc 3 1204 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL1456:
	.loc 3 1205 0
	mov	r3, r2
	mov	r2, r1
.LVL1457:
	mov	r1, r0
.LVL1458:
	movs	r0, #0
.LVL1459:
	b	FtlRead
.LVL1460:
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
	.loc 3 962 0
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
.LVL1461:
	.loc 3 970 0
	cmp	r0, #16
	.loc 3 962 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
.LCFI192:
	.pad #68
	sub	sp, sp, #68
.LCFI193:
	.loc 3 962 0
	mov	r8, r1
	str	r2, [sp, #12]
	str	r3, [sp, #4]
	.loc 3 970 0
	bne	.L1656
	.loc 3 971 0
	mov	r2, r3
.LVL1462:
	ldr	r1, [sp, #12]
	add	r0, r8, #256
.LVL1463:
	bl	FtlVendorPartWrite
.LVL1464:
.L1655:
	.loc 3 1153 0
	add	sp, sp, #68
.LCFI194:
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.LVL1465:
.L1656:
.LCFI195:
	.loc 3 974 0
	ldr	r3, [sp, #12]
.LVL1466:
	ldr	r4, .L1722
	adds	r7, r1, r3
	ldr	r3, [r4, #116]
.LVL1467:
	cmp	r7, r3
	bhi	.L1698
	.loc 3 974 0 is_stmt 0 discriminator 1
	ldr	r6, .L1722+4
	ldr	r0, [r6]
.LVL1468:
	adds	r5, r0, #1
	beq	.L1655
	.loc 3 977 0 is_stmt 1
	ldr	r3, .L1722+8
	mov	r2, #2048
	.loc 3 978 0
	ldrh	r5, [r4, #34]
	mov	r0, r8
	.loc 3 977 0
	str	r2, [r3, #-220]
	.loc 3 978 0
	mov	r1, r5
	bl	__aeabi_uidiv
.LVL1469:
	.loc 3 979 0
	mov	r1, r5
	.loc 3 978 0
	mov	r10, r0
.LVL1470:
	.loc 3 979 0
	subs	r0, r7, #1
	bl	__aeabi_uidiv
.LVL1471:
	.loc 3 980 0
	sub	r7, r0, r10
	.loc 3 983 0
	ldr	fp, [r4, #3948]
	.loc 3 980 0
	adds	r3, r7, #1
.LVL1472:
	.loc 3 982 0
	adds	r2, r7, #1
.LVL1473:
	.loc 3 980 0
	str	r3, [sp, #8]
	.loc 3 982 0
	ldr	r3, [r4, #2492]
	.loc 3 979 0
	str	r0, [sp, #20]
.LVL1474:
	.loc 3 982 0
	add	r3, r3, r2
	str	r3, [r4, #2492]
	.loc 3 983 0
	cmp	fp, #0
	beq	.L1658
	.loc 3 984 0
	ldr	r3, [fp, #16]
	cmp	r10, r3
	beq	.L1659
	.loc 3 985 0
	bl	FtlCacheWriteBack
.LVL1475:
.L1658:
	.loc 3 1008 0
	ldr	r3, [r6, #8]
	cmp	r3, #0
	beq	.L1700
	.loc 3 1009 0 discriminator 1
	ldr	r5, .L1722+12
	.loc 3 1008 0 discriminator 1
	ldrh	r2, [r4, #2328]
.LVL1476:
	.loc 3 1009 0 discriminator 1
	add	r3, r5, #48
	cmp	r2, #0
	it	eq
	moveq	r5, r3
.LVL1477:
.L1661:
	.loc 3 1012 0
	ldr	r4, .L1722
	.loc 3 1068 0
	mov	r6, r10
.LVL1478:
.L1662:
	.loc 3 1011 0
	ldr	r3, [sp, #8]
.LVL1479:
	cmp	r3, #0
	beq	.L1696
	.loc 3 1012 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #8]
.LVL1480:
	cmp	r2, r3
	bcc	.L1663
	.loc 3 1012 0 is_stmt 0 discriminator 1
	mov	r2, #1012
	ldr	r1, .L1722+16
	ldr	r0, .L1722+20
	bl	printk
.LVL1481:
.L1663:
	.loc 3 1013 0 is_stmt 1
	ldrh	r9, [r5, #4]
.LVL1482:
	cmp	r9, #0
	bne	.L1664
	.loc 3 1014 0
	bl	FtlCacheWriteBack
.LVL1483:
	.loc 3 1016 0
	ldr	r3, .L1722+12
	ldr	r7, .L1722+4
	cmp	r5, r3
	bne	.L1665
	.loc 3 1017 0
	ldrh	r9, [r4, #2376]
	cmp	r9, #0
	bne	.L1666
	.loc 3 1018 0
	add	r0, r5, #48
	bl	allocate_new_data_superblock
.LVL1484:
	.loc 3 1019 0
	str	r9, [r7, #8]
.L1666:
	.loc 3 1023 0
	ldr	r5, .L1722+12
.LVL1485:
	.loc 3 1021 0
	ldr	r0, .L1722+12
	bl	allocate_new_data_superblock
.LVL1486:
	.loc 3 1022 0
	ldr	r2, [r7, #8]
	.loc 3 1023 0
	add	r3, r5, #48
	cmp	r2, #0
	it	ne
	movne	r5, r3
.LVL1487:
.L1667:
	.loc 3 1033 0
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1664
	.loc 3 1034 0
	mov	r0, r5
	bl	allocate_new_data_superblock
.LVL1488:
.L1664:
	.loc 3 1041 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	.loc 3 1043 0
	ldrh	r3, [r5, #4]
	ldr	r1, [sp, #8]
.LVL1489:
	.loc 3 1041 0
	lsls	r2, r2, #2
	cmp	r3, r1
	it	cs
	movcs	r3, r1
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	.loc 3 1045 0
	ldrh	r3, [r4, #8]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1668
	.loc 3 1045 0 is_stmt 0 discriminator 1
	movw	r2, #1045
	ldr	r1, .L1722+16
.LVL1490:
	ldr	r0, .L1722+20
	bl	printk
.LVL1491:
.L1668:
	.loc 3 1068 0 is_stmt 1 discriminator 1
	mov	r9, #0
.LVL1492:
.L1669:
	.loc 3 1046 0 discriminator 1
	ldr	r3, [sp, #36]
	cmp	r9, r3
	bne	.L1690
.L1670:
	.loc 3 1127 0
	ldr	r3, [r4, #3948]
	cmp	r3, #0
	beq	.L1691
	.loc 3 1129 0
	ldr	r3, [sp, #8]
.LVL1493:
	.loc 3 1130 0
	subs	r9, r9, #1
	.loc 3 1129 0
	add	r3, r3, #-1
	str	r3, [sp, #8]
	.loc 3 1130 0
	bne	.L1691
.L1696:
	.loc 3 1137 0
	ldr	r3, [sp, #20]
.LVL1494:
	movs	r0, #0
	.loc 3 1139 0
	ldr	r4, .L1722
	.loc 3 1137 0
	sub	r1, r3, r10
	bl	rk_ftl_garbage_collect
.LVL1495:
	.loc 3 1139 0
	ldrh	r3, [r4, #2320]
	cmp	r3, #15
	bhi	.L1693
	.loc 3 1141 0
	movw	r5, #65535
.LVL1496:
.L1717:
	ldrh	r3, [r4, #2564]
	cmp	r3, r5
	bne	.L1697
	.loc 3 1141 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #3178]
	cmp	r3, r5
	bne	.L1697
	.loc 3 1142 0 is_stmt 1
	movs	r0, #0
	bl	List_get_gc_head_node
.LVL1497:
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.LVL1498:
.L1697:
	.loc 3 1145 0
	movs	r1, #1
	.loc 3 1143 0
	movs	r3, #128
	.loc 3 1145 0
	mov	r0, r1
	.loc 3 1143 0
	strh	r3, [r4, #3126]	@ movhi
	.loc 3 1144 0
	strh	r3, [r4, #3124]	@ movhi
	.loc 3 1145 0
	bl	rk_ftl_garbage_collect
.LVL1499:
	.loc 3 1146 0
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1500:
	.loc 3 1147 0
	ldrh	r3, [r4, #2320]
	cmp	r3, #8
	bls	.L1717
	b	.L1693
.LVL1501:
.L1659:
	.loc 3 987 0
	ldr	r3, [r4, #2496]
	.loc 3 988 0
	mov	r1, r5
	mov	r0, r8
	.loc 3 987 0
	adds	r3, r3, #1
	str	r3, [r4, #2496]
	.loc 3 988 0
	bl	__aeabi_uidivmod
.LVL1502:
	ldr	r2, [sp, #12]
.LVL1503:
	.loc 3 989 0
	subs	r5, r5, r1
	.loc 3 992 0
	ldr	r0, [fp, #8]
	.loc 3 988 0
	mov	r3, r1
	.loc 3 992 0
	ldr	r1, [sp, #4]
	cmp	r5, r2
	it	cs
	movcs	r5, r2
	lsl	r9, r5, #9
	add	r0, r0, r3, lsl #9
	mov	r2, r9
.LVL1504:
	bl	ftl_memcpy
.LVL1505:
	.loc 3 994 0
	cbnz	r7, .L1660
.LVL1506:
.L1693:
	.loc 3 995 0
	movs	r0, #0
	b	.L1655
.LVL1507:
.L1660:
	.loc 3 996 0
	ldr	r3, [sp, #12]
.LVL1508:
	.loc 3 997 0
	add	r8, r8, r5
	.loc 3 999 0
	add	r10, r10, #1
	.loc 3 996 0
	subs	r3, r3, r5
	str	r3, [sp, #12]
	.loc 3 998 0
	ldr	r3, [sp, #4]
.LVL1509:
	add	r3, r3, r9
	str	r3, [sp, #4]
	.loc 3 1000 0
	bl	FtlCacheWriteBack
.LVL1510:
	str	r7, [sp, #8]
	b	.L1658
.LVL1511:
.L1700:
	.loc 3 1006 0
	ldr	r5, .L1722+12
.LVL1512:
	b	.L1661
.LVL1513:
.L1665:
	.loc 3 1028 0
	ldrh	r2, [r4, #2328]
	.loc 3 1027 0
	str	r9, [r7, #8]
	.loc 3 1028 0
	cbnz	r2, .L1703
	.loc 3 1029 0
	mov	r0, r5
	bl	allocate_new_data_superblock
.LVL1514:
	b	.L1667
.L1703:
	mov	r5, r3
	b	.L1664
.LVL1515:
.L1690:
	.loc 3 1047 0
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1670
	.loc 3 1051 0
	movs	r2, #0
	add	r1, sp, #40
.LVL1516:
	mov	r0, r6
	movs	r7, #20
	bl	log2phys
.LVL1517:
	.loc 3 1052 0
	mov	r0, r5
	mul	r7, r7, r9
	bl	get_new_active_ppa
.LVL1518:
	.loc 3 1055 0
	ldrh	r2, [r4, #88]
	.loc 3 1054 0
	ldr	r3, [r4, #3800]
	.loc 3 1055 0
	mul	r1, r2, r9
	.loc 3 1054 0
	add	r3, r3, r7
	str	r0, [r3, #4]
	.loc 3 1056 0
	str	r6, [r3, #16]
	.loc 3 1055 0
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3772]
	add	fp, r1, r0
	str	r1, [sp, #32]
	str	fp, [r3, #12]
	.loc 3 1059 0
	movs	r1, #0
	mov	r0, fp
	bl	ftl_memset
.LVL1519:
	.loc 3 1061 0
	cmp	r6, r10
	beq	.L1671
	.loc 3 1061 0 is_stmt 0 discriminator 1
	ldr	r3, [sp, #20]
.LVL1520:
	cmp	r6, r3
	bne	.L1716
	.loc 3 1069 0 is_stmt 1
	ldr	r3, [sp, #12]
.LVL1521:
	add	r2, r8, r3
	ldrh	r3, [r4, #34]
.LVL1522:
	smulbb	r3, r3, r6
	subs	r2, r2, r3
	uxth	r3, r2
	str	r3, [sp, #16]
	.loc 3 1068 0
	movs	r3, #0
	str	r3, [sp, #24]
	b	.L1674
.L1671:
	.loc 3 1063 0
	ldrh	r2, [r4, #34]
	mov	r0, r8
	mov	r1, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidivmod
.LVL1523:
	.loc 3 1064 0
	ldr	r2, [sp, #16]
	.loc 3 1063 0
	str	r1, [sp, #24]
	.loc 3 1064 0
	subs	r2, r2, r1
	ldr	r1, [sp, #12]
.LVL1524:
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	str	r3, [sp, #16]
.L1674:
	.loc 3 1072 0
	ldrh	r3, [r4, #34]
	ldr	r2, [sp, #16]
	cmp	r2, r3
	.loc 3 1074 0
	ldr	r3, [r4, #3800]
	.loc 3 1072 0
	bne	.L1675
	.loc 3 1073 0
	cmp	r6, r10
	.loc 3 1074 0
	add	r7, r7, r3
	.loc 3 1073 0
	bne	.L1676
	.loc 3 1074 0
	ldr	r3, [sp, #4]
.LVL1525:
.L1720:
	.loc 3 1117 0
	str	r3, [r7, #8]
.LVL1526:
	b	.L1677
.LVL1527:
.L1676:
	.loc 3 1076 0
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #4]
.LVL1528:
	muls	r2, r6, r2
	sub	r2, r2, r8
	add	r2, r3, r2, lsl #9
	str	r2, [r7, #8]
.LVL1529:
.L1677:
	.loc 3 1119 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #8]
.LVL1530:
	cmp	r2, r3
	bcc	.L1687
	.loc 3 1119 0 is_stmt 0 discriminator 1
	movw	r2, #1119
	ldr	r1, .L1722+16
.LVL1531:
	ldr	r0, .L1722+20
	bl	printk
.LVL1532:
.L1687:
	.loc 3 1120 0 is_stmt 1
	ldr	r2, [sp, #32]
	movw	r3, #61589
	ldr	r1, [sp, #28]
.LVL1533:
	.loc 3 1046 0
	add	r9, r9, #1
	.loc 3 1120 0
	strh	r3, [r2, r1]	@ movhi
	.loc 3 1121 0
	ldr	r3, [r4, #2520]
	.loc 3 1122 0
	str	r6, [fp, #8]
	.loc 3 1125 0
	adds	r6, r6, #1
	.loc 3 1121 0
	str	r3, [fp, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, #0
	str	r3, [r4, #2520]
	.loc 3 1123 0
	ldr	r3, [sp, #40]
.LVL1534:
	str	r3, [fp, #12]
	.loc 3 1124 0
	ldrh	r3, [r5]
.LVL1535:
	strh	r3, [fp, #2]	@ movhi
	b	.L1669
.L1723:
	.align	2
.L1722:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2324
	.word	.LANCHOR1+574
	.word	.LC1
.LVL1536:
.L1675:
	.loc 3 1079 0
	cmp	r6, r10
	.loc 3 1080 0
	add	r3, r3, r7
	ite	eq
	ldreq	r2, [r4, #3776]
	.loc 3 1082 0
	ldrne	r2, [r4, #3768]
	str	r2, [r3, #8]
	.loc 3 1084 0
	ldr	r3, [sp, #40]
	adds	r1, r3, #1
.LVL1537:
	beq	.L1680
.LBB541:
	.loc 3 1086 0
	str	r3, [sp, #48]
	.loc 3 1090 0
	movs	r1, #1
	.loc 3 1088 0
	ldr	r3, [r4, #3800]
	.loc 3 1090 0
	add	r0, sp, #44
	.loc 3 1087 0
	str	r6, [sp, #60]
	.loc 3 1088 0
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	.loc 3 1089 0
	ldr	r3, [r3, #12]
	.loc 3 1088 0
	str	r2, [sp, #52]
	.loc 3 1090 0
	movs	r2, #0
	.loc 3 1089 0
	str	r3, [sp, #56]
	.loc 3 1090 0
	bl	FlashReadPages
.LVL1538:
	.loc 3 1091 0
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1681
	.loc 3 1092 0
	ldr	r3, [r4, #2684]
	adds	r3, r3, #1
	str	r3, [r4, #2684]
.L1684:
	ldr	r3, [sp, #16]
.LBE541:
	.loc 3 1104 0
	cmp	r6, r10
	lsl	r2, r3, #9
	bne	.L1685
	.loc 3 1105 0
	ldr	r3, [r4, #3800]
	ldr	r1, [sp, #4]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #24]
	add	r0, r0, r3, lsl #9
.L1719:
	.loc 3 1107 0
	bl	ftl_memcpy
.LVL1539:
	.loc 3 1109 0
	ldr	r3, [sp, #20]
.LVL1540:
	cmp	r6, r3
	bne	.L1677
	.loc 3 1110 0
	ldrh	r3, [r5, #4]
.LVL1541:
	cmp	r3, #0
	beq	.L1677
	.loc 3 1111 0
	ldr	r3, [r4, #3800]
	add	r7, r7, r3
	.loc 3 1112 0
	ldr	r3, .L1724
	.loc 3 1111 0
	str	r7, [r4, #3948]
	.loc 3 1112 0
	str	r5, [r3, #-236]
	b	.L1677
.L1681:
.LBB542:
	.loc 3 1094 0
	ldr	r3, [fp, #8]
	cmp	r6, r3
	beq	.L1683
	.loc 3 1095 0
	ldr	r3, [r4, #2684]
	.loc 3 1096 0
	mov	r2, r6
	ldr	r0, .L1724+4
	.loc 3 1095 0
	adds	r3, r3, #1
	str	r3, [r4, #2684]
	.loc 3 1096 0
	ldr	r1, [fp, #8]
	bl	printk
.LVL1542:
.L1683:
	.loc 3 1098 0
	ldr	r3, [fp, #8]
	cmp	r6, r3
	beq	.L1684
	.loc 3 1098 0 is_stmt 0 discriminator 1
	movw	r2, #1098
	ldr	r1, .L1724+8
	ldr	r0, .L1724+12
	bl	printk
.LVL1543:
	b	.L1684
.L1680:
.LBE542:
	.loc 3 1101 0 is_stmt 1
	ldr	r3, [r4, #3800]
	movs	r1, #0
	ldrh	r2, [r4, #86]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL1544:
	b	.L1684
.L1685:
	.loc 3 1107 0
	ldrh	r1, [r4, #34]
	ldr	r0, [sp, #4]
.LVL1545:
	ldr	r3, [r4, #3800]
	muls	r1, r6, r1
	add	r3, r3, r7
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
.LVL1546:
	b	.L1719
.LVL1547:
.L1716:
	.loc 3 1117 0
	ldr	r3, [r4, #3800]
.LVL1548:
	ldr	r2, [sp, #4]
.LVL1549:
	add	r7, r7, r3
	ldrh	r3, [r4, #34]
	muls	r3, r6, r3
	sub	r3, r3, r8
	add	r3, r2, r3, lsl #9
	b	.L1720
.LVL1550:
.L1691:
	.loc 3 1133 0
	mov	r3, r5
	movs	r2, #0
	mov	r1, r9
.LVL1551:
	ldr	r0, [r4, #3800]
	bl	FtlProgPages
.LVL1552:
	.loc 3 1134 0
	ldr	r3, [sp, #8]
.LVL1553:
	cmp	r3, r9
	bcs	.L1694
	.loc 3 1134 0 is_stmt 0 discriminator 1
	movw	r2, #1134
	ldr	r1, .L1724+8
	ldr	r0, .L1724+12
	bl	printk
.LVL1554:
.L1694:
	.loc 3 1135 0 is_stmt 1
	ldr	r3, [sp, #8]
	sub	r3, r3, r9
	str	r3, [sp, #8]
	b	.L1662
.LVL1555:
.L1698:
	.loc 3 975 0
	mov	r0, #-1
.LVL1556:
	b	.L1655
.L1725:
	.align	2
.L1724:
	.word	.LANCHOR3
	.word	.LC104
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
	.loc 3 1209 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
.LVL1557:
	.loc 3 1210 0
	mov	r3, r2
	mov	r2, r1
.LVL1558:
	mov	r1, r0
.LVL1559:
	movs	r0, #0
.LVL1560:
	b	FtlWrite
.LVL1561:
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
	.loc 2 1409 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
.LCFI196:
	.loc 2 1417 0
	movs	r1, #0
	.loc 2 1414 0
	ldr	r4, .L1748
	ldr	r3, [r4, #3240]
	.loc 2 1417 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #2300]
	.loc 2 1414 0
	str	r3, [r4, #3228]
	.loc 2 1415 0
	ldr	r3, [r4, #3244]
	.loc 2 1417 0
	lsls	r2, r2, #1
	.loc 2 1415 0
	str	r3, [r4, #3232]
	.loc 2 1417 0
	bl	ftl_memset
.LVL1562:
	.loc 2 1418 0
	ldrh	r0, [r4, #2552]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1728
.LVL1563:
.L1736:
	.loc 2 1419 0
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.LVL1564:
.L1728:
	.loc 2 1430 0
	ldr	r7, .L1748+4
	.loc 2 1421 0
	movs	r1, #1
	.loc 2 1427 0
	addw	r6, r4, #3220
	.loc 2 1421 0
	bl	FtlGetLastWrittenPage
.LVL1565:
	sxth	r5, r0
.LVL1566:
	.loc 2 1422 0
	adds	r0, r0, #1
	strh	r0, [r4, #2554]	@ movhi
.L1730:
	.loc 2 1424 0
	cmp	r5, #0
	bge	.L1733
	.loc 2 1437 0
	movw	r2, #1437
	ldr	r1, .L1748+8
	ldr	r0, .L1748+12
	bl	printk
.LVL1567:
.L1732:
	.loc 2 1439 0
	ldrh	r3, [r4, #16]
	ldrh	r2, [r4, #86]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1735
	.loc 2 1439 0 is_stmt 0 discriminator 1
	movw	r2, #1439
	ldr	r1, .L1748+8
	ldr	r0, .L1748+12
	bl	printk
.LVL1568:
.L1735:
	.loc 2 1441 0 is_stmt 1
	movs	r2, #48
	ldr	r1, [r4, #3228]
	ldr	r0, .L1748+16
	bl	ftl_memcpy
.LVL1569:
	.loc 2 1442 0
	ldrh	r2, [r4, #16]
	ldr	r1, [r4, #3228]
	ldr	r0, [r4, #2300]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
.LVL1570:
	.loc 2 1443 0
	ldrh	r1, [r4, #16]
	ldr	r3, [r4, #3228]
	ldr	r0, [r4]
	lsrs	r2, r1, #3
	adds	r1, r1, #24
	lsls	r1, r1, #1
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
.LVL1571:
	.loc 2 1445 0
	ldr	r2, [r4, #2244]
	ldr	r3, .L1748+4
	cmp	r2, r3
	bne	.L1736
	.loc 2 1449 0
	ldrb	r2, [r4, #2254]	@ zero_extendqisi2
	ldrh	r3, [r4, #30]
	.loc 2 1448 0
	ldrh	r5, [r4, #2252]
.LVL1572:
	.loc 2 1449 0
	cmp	r2, r3
	.loc 2 1448 0
	strh	r5, [r4, #2558]	@ movhi
	.loc 2 1449 0
	bne	.L1736
	.loc 2 1452 0
	ldr	r3, .L1748+20
	.loc 2 1454 0
	ldrh	r2, [r4, #34]
	.loc 2 1455 0
	ldr	r6, [r4, #20]
	.loc 2 1452 0
	str	r5, [r3, #-216]
	.loc 2 1453 0
	ldrh	r3, [r4, #78]
	.loc 2 1455 0
	ldrh	r0, [r4, #134]
	ldrh	r1, [r4, #8]
	.loc 2 1453 0
	muls	r3, r5, r3
	.loc 2 1455 0
	subs	r0, r6, r0
	subs	r0, r0, r5
	.loc 2 1453 0
	str	r3, [r4, #2480]
	.loc 2 1454 0
	muls	r3, r2, r3
	str	r3, [r4, #116]
	.loc 2 1455 0
	bl	__aeabi_uidiv
.LVL1573:
	.loc 2 1461 0
	cmp	r5, r6
	.loc 2 1455 0
	strh	r0, [r4, #2548]	@ movhi
	.loc 2 1461 0
	bls	.L1737
	.loc 2 1461 0 is_stmt 0 discriminator 1
	movw	r2, #1461
	ldr	r1, .L1748+8
	ldr	r0, .L1748+12
	bl	printk
.LVL1574:
.L1737:
	.loc 2 1464 0 is_stmt 1
	ldrh	r3, [r4, #2260]
	.loc 2 1463 0
	ldrh	r1, [r4, #2258]
	.loc 2 1464 0
	lsrs	r2, r3, #6
	.loc 2 1465 0
	and	r3, r3, #63
	strb	r3, [r4, #2330]
	.loc 2 1464 0
	strh	r2, [r4, #2326]	@ movhi
	.loc 2 1473 0
	ldrh	r2, [r4, #2262]
	.loc 2 1466 0
	ldrb	r3, [r4, #2255]	@ zero_extendqisi2
	.loc 2 1463 0
	strh	r1, [r4, #2324]	@ movhi
	.loc 2 1473 0
	strh	r2, [r4, #2372]	@ movhi
	.loc 2 1474 0
	ldrh	r2, [r4, #2264]
	.loc 2 1466 0
	strb	r3, [r4, #2332]
	.loc 2 1468 0
	movw	r3, #65535
	strh	r3, [r4, #2564]	@ movhi
	.loc 2 1469 0
	movs	r3, #0
	strh	r3, [r4, #2566]	@ movhi
	.loc 2 1474 0
	lsrs	r0, r2, #6
	.loc 2 1475 0
	and	r2, r2, #63
	strb	r2, [r4, #2378]
	.loc 2 1476 0
	ldrb	r2, [r4, #2256]	@ zero_extendqisi2
	.loc 2 1474 0
	strh	r0, [r4, #2374]	@ movhi
	.loc 2 1470 0
	strb	r3, [r4, #2570]
	.loc 2 1476 0
	strb	r2, [r4, #2380]
	.loc 2 1478 0
	ldrh	r2, [r4, #2266]
	.loc 2 1471 0
	strb	r3, [r4, #2572]
	.loc 2 1483 0
	str	r3, [r4, #2504]
	.loc 2 1478 0
	strh	r2, [r4, #2420]	@ movhi
	.loc 2 1479 0
	ldrh	r2, [r4, #2268]
	.loc 2 1484 0
	str	r3, [r4, #2492]
	.loc 2 1485 0
	str	r3, [r4, #2484]
	.loc 2 1486 0
	str	r3, [r4, #2500]
	.loc 2 1479 0
	lsrs	r0, r2, #6
	.loc 2 1480 0
	and	r2, r2, #63
	strb	r2, [r4, #2426]
	.loc 2 1481 0
	ldrb	r2, [r4, #2257]	@ zero_extendqisi2
	.loc 2 1489 0
	str	r3, [r4, #2528]
	.loc 2 1479 0
	strh	r0, [r4, #2422]	@ movhi
	.loc 2 1481 0
	strb	r2, [r4, #2428]
	.loc 2 1488 0
	ldr	r2, [r4, #2276]
	str	r2, [r4, #2524]
	.loc 2 1490 0
	str	r3, [r4, #2540]
	.loc 2 1493 0
	ldr	r2, [r4, #2516]
	.loc 2 1491 0
	str	r3, [r4, #2496]
	.loc 2 1493 0
	ldr	r3, [r4, #2284]
	cmp	r3, r2
	.loc 2 1496 0
	ldr	r2, [r4, #2520]
	.loc 2 1494 0
	it	hi
	strhi	r3, [r4, #2516]
	.loc 2 1496 0
	ldr	r3, [r4, #2280]
	cmp	r3, r2
	.loc 2 1497 0
	it	hi
	strhi	r3, [r4, #2520]
	.loc 2 1499 0
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1740
	.loc 2 1500 0
	ldr	r0, .L1748+24
	bl	make_superblock
.LVL1575:
.L1740:
	.loc 2 1502 0
	ldrh	r2, [r4, #2372]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1741
	.loc 2 1503 0
	ldr	r0, .L1748+28
	bl	make_superblock
.LVL1576:
.L1741:
	.loc 2 1505 0
	ldrh	r2, [r4, #2420]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1742
	.loc 2 1506 0
	ldr	r0, .L1748+32
	bl	make_superblock
.LVL1577:
.L1742:
	.loc 2 1508 0
	ldrh	r2, [r4, #2564]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1743
	.loc 2 1509 0
	ldr	r0, .L1748+36
	bl	make_superblock
.LVL1578:
.L1743:
	.loc 2 1511 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.LVL1579:
.L1733:
	.loc 2 1425 0
	ldrh	r3, [r4, #2552]
	.loc 2 1427 0
	movs	r2, #1
	mov	r1, r2
	mov	r0, r6
	.loc 2 1425 0
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3224]
	.loc 2 1426 0
	ldr	r3, [r4, #3240]
	str	r3, [r4, #3228]
	.loc 2 1427 0
	bl	FlashReadPages
.LVL1580:
	.loc 2 1430 0
	ldr	r3, [r4, #3220]
	adds	r3, r3, #1
	beq	.L1731
	.loc 2 1431 0 discriminator 1
	ldr	r3, [r4, #3240]
	.loc 2 1430 0 discriminator 1
	ldr	r3, [r3]
	cmp	r3, r7
	bne	.L1731
	.loc 2 1432 0
	ldr	r3, [r4, #3244]
	ldrh	r2, [r3]
	.loc 2 1431 0
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1732
.L1731:
	subs	r5, r5, #1
.LVL1581:
	sxth	r5, r5
.LVL1582:
	b	.L1730
.L1749:
	.align	2
.L1748:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR1+583
	.word	.LC1
	.word	.LANCHOR0+2244
	.word	.LANCHOR3
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+2420
	.word	.LANCHOR0+2564
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
	.loc 2 2356 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
.LCFI197:
	.loc 2 2360 0
	movs	r3, #0
	ldr	r4, .L1766
	.loc 2 2362 0
	ldrh	r0, [r4, #12]
	.loc 2 2360 0
	strh	r3, [r4, #3968]	@ movhi
	.loc 2 2362 0
	bl	FtlFreeSysBlkQueueInit
.LVL1583:
	.loc 2 2363 0
	bl	FtlScanSysBlk
.LVL1584:
	.loc 2 2364 0
	ldrh	r2, [r4, #2552]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1751
.L1753:
	.loc 2 2365 0
	mov	r5, #-1
.L1750:
	.loc 2 2448 0
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1751:
	.loc 2 2367 0
	bl	FtlLoadSysInfo
.LVL1585:
	mov	r5, r0
	cmp	r0, #0
	bne	.L1753
	.loc 2 2370 0
	bl	FtlLoadMapInfo
.LVL1586:
	.loc 2 2371 0
	bl	FtlLoadVonderInfo
.LVL1587:
	.loc 2 2372 0
	bl	Ftl_load_ext_data
.LVL1588:
	.loc 2 2374 0
	bl	FtlLoadEctTbl
.LVL1589:
	.loc 2 2375 0
	bl	FtlFreeSysBLkSort
.LVL1590:
	.loc 2 2377 0
	bl	SupperBlkListInit
.LVL1591:
	.loc 2 2378 0
	bl	FtlPowerLostRecovery
.LVL1592:
	.loc 2 2380 0
	movs	r0, #1
	bl	FtlUpdateVaildLpn
.LVL1593:
.LBB546:
.LBB547:
	.loc 2 2382 0
	ldrh	r1, [r4, #114]
	mov	r3, r5
.LVL1594:
	.loc 2 2383 0
	ldr	r2, [r4, #2468]
	movs	r0, #12
.L1754:
	.loc 2 2382 0
	cmp	r3, r1
	bge	.L1759
	.loc 2 2383 0
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1755
.L1759:
	.loc 2 2386 0
	ldrh	r2, [r4, #2272]
	.loc 2 2388 0
	cmp	r3, r1
	.loc 2 2386 0
	add	r2, r2, #1
	strh	r2, [r4, #2272]	@ movhi
	.loc 2 2388 0
	bge	.L1765
.L1756:
	.loc 2 2389 0
	ldr	r0, .L1766+4
	bl	FtlSuperblockPowerLostFix
.LVL1595:
	.loc 2 2390 0
	ldr	r0, .L1766+8
	bl	FtlSuperblockPowerLostFix
.LVL1596:
.LBB548:
	.loc 2 2396 0
	ldrh	r1, [r4, #2324]
	ldr	r2, [r4, #2300]
	ldrh	r0, [r4, #2328]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	.loc 2 2397 0
	ldrh	r3, [r4, #78]
	.loc 2 2402 0
	ldr	r1, [r4, #2300]
	ldrh	r0, [r4, #2372]
	.loc 2 2397 0
	strh	r3, [r4, #2326]	@ movhi
	.loc 2 2398 0
	movs	r3, #0
	strb	r3, [r4, #2330]
	.loc 2 2399 0
	strh	r3, [r4, #2328]	@ movhi
	.loc 2 2402 0
	ldrh	r6, [r4, #2376]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	.loc 2 2403 0
	ldrh	r2, [r4, #78]
.LBE548:
	.loc 2 2409 0
	ldr	r0, .L1766+12
.LBB549:
	.loc 2 2404 0
	strb	r3, [r4, #2378]
	.loc 2 2403 0
	strh	r2, [r4, #2374]	@ movhi
	.loc 2 2405 0
	strh	r3, [r4, #2376]	@ movhi
.LBE549:
	.loc 2 2409 0
	bl	FtlMapBlkWriteDump_data
.LVL1597:
	.loc 2 2410 0
	ldr	r0, .L1766+16
	bl	FtlMapBlkWriteDump_data
.LVL1598:
	.loc 2 2422 0
	ldrh	r3, [r4, #2274]
	adds	r3, r3, #1
	strh	r3, [r4, #2274]	@ movhi
	.loc 2 2423 0
	bl	l2p_flush
.LVL1599:
	.loc 2 2424 0
	bl	FtlVpcTblFlush
.LVL1600:
	.loc 2 2425 0
	bl	FtlVpcTblFlush
.LVL1601:
	b	.L1760
.LVL1602:
.L1755:
	.loc 2 2382 0
	adds	r3, r3, #1
	b	.L1754
.L1765:
	.loc 2 2388 0
	ldrh	r3, [r4, #3968]
.LVL1603:
	cmp	r3, #0
	bne	.L1756
.L1760:
	.loc 2 2429 0
	ldrh	r0, [r4, #2324]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1761
	ldrh	r3, [r4, #2328]
	cbnz	r3, .L1761
	.loc 2 2430 0
	ldrh	r3, [r4, #2376]
	cbnz	r3, .L1761
	.loc 2 2432 0
	bl	FtlGcRefreshBlock
.LVL1604:
	.loc 2 2433 0
	ldrh	r0, [r4, #2372]
	bl	FtlGcRefreshBlock
.LVL1605:
	.loc 2 2434 0
	ldr	r0, .L1766+4
	bl	allocate_new_data_superblock
.LVL1606:
	.loc 2 2435 0
	ldr	r0, .L1766+8
	bl	allocate_new_data_superblock
.LVL1607:
.L1761:
	.loc 2 2440 0
	ldrh	r3, [r4, #2272]
	lsls	r3, r3, #27
	bne	.L1750
	.loc 2 2441 0
	bl	FtlVpcCheckAndModify
.LVL1608:
	b	.L1750
.L1767:
	.align	2
.L1766:
	.word	.LANCHOR0
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+3864
	.word	.LANCHOR0+3908
.LBE547:
.LBE546:
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
.LCFI198:
	.loc 3 629 0
	movs	r3, #0
	ldr	r4, .L1792
	.loc 3 631 0
	ldrh	r0, [r4, #12]
	.loc 3 629 0
	str	r3, [r4, #2516]
	.loc 3 630 0
	str	r3, [r4, #2520]
	.loc 3 631 0
	bl	FtlFreeSysBlkQueueInit
.LVL1609:
	.loc 3 632 0
	bl	FtlLoadBbt
.LVL1610:
	cbz	r0, .L1769
	.loc 3 633 0
	bl	FtlMakeBbt
.LVL1611:
.L1769:
	.loc 3 637 0 discriminator 1
	ldr	r0, .L1792+4
	.loc 3 623 0 discriminator 1
	movs	r2, #0
.L1770:
	.loc 3 635 0 discriminator 1
	ldrh	r1, [r4, #34]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1771
	.loc 3 642 0
	ldrh	r6, [r4, #16]
	.loc 3 641 0
	movs	r5, #0
.LVL1612:
.L1772:
	.loc 3 642 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r6
	bhi	.L1773
	.loc 3 645 0
	ldrh	r1, [r4, #8]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1774
.L1778:
	.loc 3 641 0
	movs	r5, #0
.LVL1613:
	mov	r6, r5
.LVL1614:
.L1775:
	.loc 3 656 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1779
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
.LVL1615:
	.loc 3 662 0
	ubfx	r7, r0, #5, #16
	.loc 3 661 0
	mov	r8, r0
	.loc 3 662 0
	add	r3, r7, #36
	.loc 3 661 0
	str	r0, [r4, #2480]
	.loc 3 662 0
	strh	r3, [r4, #2548]	@ movhi
	.loc 3 663 0
	movs	r3, #24
	muls	r3, r5, r3
	cmp	r6, r3
	ble	.L1780
	.loc 3 664 0
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
.LVL1616:
	str	r0, [r4, #2480]
	.loc 3 665 0
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2548]	@ movhi
.L1780:
	.loc 3 668 0
	ldrh	r3, [r4, #70]
	cbz	r3, .L1782
	.loc 3 669 0
	ldrh	r2, [r4, #2548]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #2548]	@ movhi
	.loc 3 670 0
	mul	r2, r5, r3
	cmp	r6, r2
	.loc 3 672 0
	itttt	lt
	addlt	r3, r3, #32
	.loc 3 671 0
	strlt	r8, [r4, #2480]
	.loc 3 672 0
	addlt	r3, r3, r7
	strhlt	r3, [r4, #2548]	@ movhi
.L1782:
	.loc 3 677 0
	ldrh	r2, [r4, #2548]
	.loc 3 689 0
	movw	r6, #65535
.LVL1617:
	.loc 3 677 0
	ldr	r3, [r4, #2480]
	ldr	r7, .L1792+8
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
	str	r5, [r4, #2480]
	.loc 3 679 0
	muls	r5, r3, r5
	str	r5, [r4, #116]
	mov	r5, r7
	.loc 3 684 0
	bl	FtlBbmTblFlush
.LVL1618:
	.loc 3 687 0
	ldrh	r2, [r4, #18]
	movs	r1, #0
	ldr	r0, [r4, #2300]
	.loc 3 701 0
	ldr	r7, .L1792+12
	.loc 3 687 0
	lsls	r2, r2, #1
	bl	ftl_memset
.LVL1619:
	.loc 3 698 0
	ldrh	r2, [r4, #16]
	.loc 3 688 0
	movs	r3, #0
	str	r3, [r4, #2476]
	.loc 3 698 0
	movs	r1, #255
	.loc 3 690 0
	strh	r3, [r4, #2566]	@ movhi
	.loc 3 691 0
	strb	r3, [r4, #2570]
	.loc 3 692 0
	strb	r3, [r4, #2572]
	.loc 3 698 0
	lsrs	r2, r2, #3
	.loc 3 694 0
	strh	r3, [r4, #2326]	@ movhi
	.loc 3 695 0
	strb	r3, [r4, #2330]
	.loc 3 696 0
	strh	r3, [r4, #2324]	@ movhi
	.loc 3 697 0
	movs	r3, #1
	.loc 3 698 0
	ldr	r0, [r4]
	.loc 3 689 0
	strh	r6, [r4, #2564]	@ movhi
	.loc 3 697 0
	strb	r3, [r4, #2332]
	.loc 3 698 0
	bl	ftl_memset
.LVL1620:
.L1784:
	.loc 3 701 0
	mov	r0, r7
	bl	make_superblock
.LVL1621:
	.loc 3 702 0
	ldrb	r3, [r4, #2331]	@ zero_extendqisi2
	ldrh	r2, [r4, #2324]
	cmp	r3, #0
	bne	.L1785
	.loc 3 705 0
	ldr	r3, [r4, #2300]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	.loc 3 706 0
	ldrh	r3, [r4, #2324]
	adds	r3, r3, #1
	strh	r3, [r4, #2324]	@ movhi
	.loc 3 707 0
	b	.L1784
.L1771:
	.loc 3 636 0 discriminator 3
	ldr	r5, [r4, #3776]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	.loc 3 637 0 discriminator 3
	ldr	r1, [r4, #3768]
	str	r0, [r1, r3, lsl #2]
	b	.L1770
.LVL1622:
.L1773:
	.loc 3 643 0 discriminator 3
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
.LVL1623:
	.loc 3 642 0 discriminator 3
	adds	r6, r6, #1
	.loc 3 643 0 discriminator 3
	add	r5, r5, r0
.LVL1624:
	uxth	r5, r5
.LVL1625:
	.loc 3 642 0 discriminator 3
	uxth	r6, r6
	b	.L1772
.L1774:
	.loc 3 647 0
	mov	r0, r5
	bl	__aeabi_uidiv
.LVL1626:
	ldr	r3, [r4, #108]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
.LVL1627:
	.loc 3 648 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1628:
	.loc 3 650 0
	ldrh	r5, [r4, #16]
.LVL1629:
.L1776:
	.loc 3 650 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r5
	bls	.L1778
	.loc 3 651 0 is_stmt 1 discriminator 3
	mov	r0, r5
	movs	r1, #1
	.loc 3 650 0 discriminator 3
	adds	r5, r5, #1
	.loc 3 651 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1630:
	.loc 3 650 0 discriminator 3
	uxth	r5, r5
	b	.L1776
.LVL1631:
.L1779:
	.loc 3 657 0 discriminator 3
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
.LVL1632:
	add	r6, r6, r0
.LVL1633:
	uxth	r6, r6
.LVL1634:
	b	.L1775
.LVL1635:
.L1785:
	.loc 3 709 0
	ldr	r3, [r4, #2516]
	.loc 3 721 0
	movw	r6, #65535
	.loc 3 710 0
	ldrh	r1, [r4, #2328]
	.loc 3 717 0
	ldr	r7, .L1792+16
	.loc 3 709 0
	str	r3, [r4, #2336]
	adds	r3, r3, #1
	str	r3, [r4, #2516]
	.loc 3 710 0
	ldr	r3, [r4, #2300]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	.loc 3 712 0
	movs	r3, #0
	strh	r3, [r4, #2374]	@ movhi
	.loc 3 713 0
	strb	r3, [r4, #2378]
	.loc 3 714 0
	ldrh	r3, [r4, #2324]
	adds	r3, r3, #1
	strh	r3, [r4, #2372]	@ movhi
	.loc 3 715 0
	movs	r3, #1
	strb	r3, [r4, #2380]
.L1786:
	.loc 3 717 0
	mov	r0, r7
	bl	make_superblock
.LVL1636:
	.loc 3 718 0
	ldrb	r3, [r4, #2379]	@ zero_extendqisi2
	ldrh	r2, [r4, #2372]
	cbnz	r3, .L1787
	.loc 3 721 0
	ldr	r3, [r4, #2300]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	.loc 3 722 0
	ldrh	r3, [r4, #2372]
	adds	r3, r3, #1
	strh	r3, [r4, #2372]	@ movhi
	.loc 3 723 0
	b	.L1786
.L1787:
	.loc 3 725 0
	ldr	r3, [r4, #2516]
	.loc 3 727 0
	movw	r6, #65535
	.loc 3 726 0
	ldrh	r1, [r4, #2376]
	.loc 3 725 0
	str	r3, [r4, #2384]
	adds	r3, r3, #1
	str	r3, [r4, #2516]
	.loc 3 726 0
	ldr	r3, [r4, #2300]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	.loc 3 727 0
	strh	r6, [r4, #2420]	@ movhi
	.loc 3 730 0
	bl	FtlFreeSysBlkQueueOut
.LVL1637:
	.loc 3 731 0
	movs	r3, #0
	.loc 3 730 0
	strh	r0, [r4, #2552]	@ movhi
	.loc 3 731 0
	strh	r3, [r4, #2554]	@ movhi
	.loc 3 733 0
	ldr	r3, [r5, #-216]
	.loc 3 732 0
	strh	r6, [r4, #2556]	@ movhi
	.loc 3 733 0
	strh	r3, [r4, #2558]	@ movhi
	.loc 3 734 0
	ldr	r3, [r4, #2516]
	str	r3, [r4, #2560]
	adds	r3, r3, #1
	str	r3, [r4, #2516]
	.loc 3 735 0
	bl	FtlVpcTblFlush
.LVL1638:
	.loc 3 736 0
	bl	FtlSysBlkInit
.LVL1639:
	cbnz	r0, .L1788
	.loc 3 737 0
	ldr	r3, .L1792+20
	movs	r2, #1
	str	r2, [r3]
.L1788:
	.loc 3 739 0
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1793:
	.align	2
.L1792:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR3
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2372
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
.LCFI199:
	.loc 3 746 0
	mov	r3, #-1
	ldr	r5, .L1800
	.loc 3 753 0
	ldr	r4, .L1800+4
	.loc 3 749 0
	ldr	r1, .L1800+8
	ldr	r0, .L1800+12
	.loc 3 746 0
	str	r3, [r5]
	.loc 3 749 0
	bl	printk
.LVL1640:
	.loc 3 750 0
	ldr	r0, .L1800+16
	bl	FtlConstantsInit
.LVL1641:
	.loc 3 751 0
	bl	FtlMemInit
.LVL1642:
	.loc 3 752 0
	bl	FtlVariablesInit
.LVL1643:
	.loc 3 753 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1644:
	.loc 3 756 0
	bl	FtlLoadBbt
.LVL1645:
	cbnz	r0, .L1798
	.loc 3 767 0
	bl	FtlSysBlkInit
.LVL1646:
	cbnz	r0, .L1798
	.loc 3 779 0
	movs	r3, #1
	str	r3, [r5]
	.loc 3 783 0
	ldrh	r3, [r4, #2320]
	cmp	r3, #15
	bhi	.L1798
	movw	r4, #8129
.L1797:
.LBB550:
	.loc 3 786 0 discriminator 3
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1647:
	.loc 3 785 0 discriminator 3
	subs	r4, r4, #1
	bne	.L1797
.L1798:
.LBE550:
	.loc 3 794 0
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1801:
	.align	2
.L1800:
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
.LVL1648:
	.loc 1 212 0
	b	memcmp
.LVL1649:
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
	.type	__func__.6616, %object
	.size	__func__.6616, 17
__func__.6616:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6611, %object
	.size	__func__.6611, 17
__func__.6611:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6647, %object
	.size	__func__.6647, 17
__func__.6647:
	.ascii	"List_remove_node\000"
	.type	__func__.6679, %object
	.size	__func__.6679, 22
__func__.6679:
	.ascii	"List_update_data_list\000"
	.type	__func__.6785, %object
	.size	__func__.6785, 22
__func__.6785:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7108, %object
	.size	__func__.7108, 16
__func__.7108:
	.ascii	"make_superblock\000"
	.type	__func__.7283, %object
	.size	__func__.7283, 19
__func__.7283:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.7523, %object
	.size	__func__.7523, 15
__func__.7523:
	.ascii	"FlashReadPages\000"
	.type	__func__.7540, %object
	.size	__func__.7540, 15
__func__.7540:
	.ascii	"FlashProgPages\000"
	.type	__func__.6721, %object
	.size	__func__.6721, 26
__func__.6721:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6235, %object
	.size	__func__.6235, 11
__func__.6235:
	.ascii	"FtlMemInit\000"
	.type	__func__.6451, %object
	.size	__func__.6451, 14
__func__.6451:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.6688, %object
	.size	__func__.6688, 16
__func__.6688:
	.ascii	"load_l2p_region\000"
	.type	__func__.6746, %object
	.size	__func__.6746, 31
__func__.6746:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6760, %object
	.size	__func__.6760, 16
__func__.6760:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6732, %object
	.size	__func__.6732, 15
__func__.6732:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6802, %object
	.size	__func__.6802, 9
__func__.6802:
	.ascii	"log2phys\000"
	.type	__func__.7017, %object
	.size	__func__.7017, 16
__func__.7017:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7170, %object
	.size	__func__.7170, 14
__func__.7170:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.6897, %object
	.size	__func__.6897, 14
__func__.6897:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7129, %object
	.size	__func__.7129, 18
__func__.7129:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.7255, %object
	.size	__func__.7255, 25
__func__.7255:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7296, %object
	.size	__func__.7296, 16
__func__.7296:
	.ascii	"update_vpc_list\000"
	.type	__func__.7303, %object
	.size	__func__.7303, 20
__func__.7303:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7051, %object
	.size	__func__.7051, 22
__func__.7051:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.6494, %object
	.size	__func__.6494, 11
__func__.6494:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.6875, %object
	.size	__func__.6875, 15
__func__.6875:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7154, %object
	.size	__func__.7154, 21
__func__.7154:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7276, %object
	.size	__func__.7276, 29
__func__.7276:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6359, %object
	.size	__func__.6359, 13
__func__.6359:
	.ascii	"FtlProgPages\000"
	.type	__func__.7373, %object
	.size	__func__.7373, 19
__func__.7373:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7479, %object
	.size	__func__.7479, 23
__func__.7479:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6387, %object
	.size	__func__.6387, 9
__func__.6387:
	.ascii	"FtlWrite\000"
	.type	__func__.6954, %object
	.size	__func__.6954, 15
__func__.6954:
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
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
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
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
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
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
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
	.ascii	"SFTL version: 5.0.44 20180713\000"
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
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC79:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC80:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC81:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC82:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC83:
	.ascii	"spare:\000"
.LC84:
	.ascii	"map_ppn:\000"
.LC85:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC86:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC87:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC88:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC89:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC90:
	.ascii	"...%s enter...\012\000"
.LC91:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC92:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC93:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC94:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC95:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC96:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC97:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC98:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC99:
	.ascii	"no ect\000"
.LC100:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC101:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC102:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC103:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC104:
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
.LEFDE44:
.LSFDE46:
	.4byte	.LEFDE46-.LASFDE46
.LASFDE46:
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
.LEFDE46:
.LSFDE48:
	.4byte	.LEFDE48-.LASFDE48
.LASFDE48:
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
.LEFDE48:
.LSFDE50:
	.4byte	.LEFDE50-.LASFDE50
.LASFDE50:
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
.LEFDE50:
.LSFDE52:
	.4byte	.LEFDE52-.LASFDE52
.LASFDE52:
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
.LEFDE52:
.LSFDE54:
	.4byte	.LEFDE54-.LASFDE54
.LASFDE54:
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
.LEFDE54:
.LSFDE56:
	.4byte	.LEFDE56-.LASFDE56
.LASFDE56:
	.4byte	.Lframe0
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.align	2
.LEFDE56:
.LSFDE58:
	.4byte	.LEFDE58-.LASFDE58
.LASFDE58:
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
.LEFDE58:
.LSFDE60:
	.4byte	.LEFDE60-.LASFDE60
.LASFDE60:
	.4byte	.Lframe0
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.byte	0x4
	.4byte	.LCFI25-.LFB202
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
.LEFDE60:
.LSFDE62:
	.4byte	.LEFDE62-.LASFDE62
.LASFDE62:
	.4byte	.Lframe0
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.byte	0x4
	.4byte	.LCFI26-.LFB210
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
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.byte	0x4
	.4byte	.LCFI27-.LFB216
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE64:
.LSFDE66:
	.4byte	.LEFDE66-.LASFDE66
.LASFDE66:
	.4byte	.Lframe0
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.align	2
.LEFDE66:
.LSFDE68:
	.4byte	.LEFDE68-.LASFDE68
.LASFDE68:
	.4byte	.Lframe0
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.align	2
.LEFDE68:
.LSFDE70:
	.4byte	.LEFDE70-.LASFDE70
.LASFDE70:
	.4byte	.Lframe0
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.byte	0x4
	.4byte	.LCFI28-.LFB229
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
.LEFDE70:
.LSFDE72:
	.4byte	.LEFDE72-.LASFDE72
.LASFDE72:
	.4byte	.Lframe0
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.byte	0x4
	.4byte	.LCFI29-.LFB232
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE72:
.LSFDE74:
	.4byte	.LEFDE74-.LASFDE74
.LASFDE74:
	.4byte	.Lframe0
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.byte	0x4
	.4byte	.LCFI30-.LFB234
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
.LEFDE74:
.LSFDE76:
	.4byte	.LEFDE76-.LASFDE76
.LASFDE76:
	.4byte	.Lframe0
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.byte	0x4
	.4byte	.LCFI31-.LFB243
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
.LEFDE76:
.LSFDE78:
	.4byte	.LEFDE78-.LASFDE78
.LASFDE78:
	.4byte	.Lframe0
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.align	2
.LEFDE78:
.LSFDE80:
	.4byte	.LEFDE80-.LASFDE80
.LASFDE80:
	.4byte	.Lframe0
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.byte	0x4
	.4byte	.LCFI32-.LFB245
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
.LEFDE80:
.LSFDE82:
	.4byte	.LEFDE82-.LASFDE82
.LASFDE82:
	.4byte	.Lframe0
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.byte	0x4
	.4byte	.LCFI33-.LFB150
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
	.4byte	.LCFI34-.LCFI33
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI35-.LCFI34
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI36-.LCFI35
	.byte	0xb
	.align	2
.LEFDE82:
.LSFDE84:
	.4byte	.LEFDE84-.LASFDE84
.LASFDE84:
	.4byte	.Lframe0
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.byte	0x4
	.4byte	.LCFI37-.LFB151
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
.LEFDE84:
.LSFDE86:
	.4byte	.LEFDE86-.LASFDE86
.LASFDE86:
	.4byte	.Lframe0
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.byte	0x4
	.4byte	.LCFI38-.LFB246
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
	.4byte	.LCFI39-.LCFI38
	.byte	0xe
	.uleb128 0x40
	.byte	0x4
	.4byte	.LCFI40-.LCFI39
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI41-.LCFI40
	.byte	0xb
	.align	2
.LEFDE86:
.LSFDE88:
	.4byte	.LEFDE88-.LASFDE88
.LASFDE88:
	.4byte	.Lframe0
	.4byte	.LFB247
	.4byte	.LFE247-.LFB247
	.byte	0x4
	.4byte	.LCFI42-.LFB247
	.byte	0xe
	.uleb128 0x8
	.byte	0x83
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE88:
.LSFDE90:
	.4byte	.LEFDE90-.LASFDE90
.LASFDE90:
	.4byte	.Lframe0
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.byte	0x4
	.4byte	.LCFI43-.LFB250
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
.LEFDE90:
.LSFDE92:
	.4byte	.LEFDE92-.LASFDE92
.LASFDE92:
	.4byte	.Lframe0
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.byte	0x4
	.4byte	.LCFI44-.LFB253
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
.LEFDE92:
.LSFDE94:
	.4byte	.LEFDE94-.LASFDE94
.LASFDE94:
	.4byte	.Lframe0
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.byte	0x4
	.4byte	.LCFI45-.LFB254
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
.LEFDE94:
.LSFDE96:
	.4byte	.LEFDE96-.LASFDE96
.LASFDE96:
	.4byte	.Lframe0
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.byte	0x4
	.4byte	.LCFI46-.LFB255
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
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.align	2
.LEFDE98:
.LSFDE100:
	.4byte	.LEFDE100-.LASFDE100
.LASFDE100:
	.4byte	.Lframe0
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.byte	0x4
	.4byte	.LCFI47-.LFB257
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
.LEFDE100:
.LSFDE102:
	.4byte	.LEFDE102-.LASFDE102
.LASFDE102:
	.4byte	.Lframe0
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.byte	0x4
	.4byte	.LCFI48-.LFB264
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.align	2
.LEFDE102:
.LSFDE104:
	.4byte	.LEFDE104-.LASFDE104
.LASFDE104:
	.4byte	.Lframe0
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.byte	0x4
	.4byte	.LCFI49-.LFB265
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
.LEFDE104:
.LSFDE106:
	.4byte	.LEFDE106-.LASFDE106
.LASFDE106:
	.4byte	.Lframe0
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.byte	0x4
	.4byte	.LCFI50-.LFB266
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
	.4byte	.LFB272
	.4byte	.LFE272-.LFB272
	.byte	0x4
	.4byte	.LCFI51-.LFB272
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
	.4byte	.LCFI52-.LCFI51
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI53-.LCFI52
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI54-.LCFI53
	.byte	0xb
	.align	2
.LEFDE108:
.LSFDE110:
	.4byte	.LEFDE110-.LASFDE110
.LASFDE110:
	.4byte	.Lframe0
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.byte	0x4
	.4byte	.LCFI55-.LFB180
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
.LEFDE110:
.LSFDE112:
	.4byte	.LEFDE112-.LASFDE112
.LASFDE112:
	.4byte	.Lframe0
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.byte	0x4
	.4byte	.LCFI56-.LFB186
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
	.4byte	.LCFI57-.LCFI56
	.byte	0xe
	.uleb128 0x70
	.byte	0x4
	.4byte	.LCFI58-.LCFI57
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI59-.LCFI58
	.byte	0xb
	.align	2
.LEFDE112:
.LSFDE114:
	.4byte	.LEFDE114-.LASFDE114
.LASFDE114:
	.4byte	.Lframe0
	.4byte	.LFB273
	.4byte	.LFE273-.LFB273
	.byte	0x4
	.4byte	.LCFI60-.LFB273
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
	.4byte	.LCFI61-.LCFI60
	.byte	0xe
	.uleb128 0x50
	.byte	0x4
	.4byte	.LCFI62-.LCFI61
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI63-.LCFI62
	.byte	0xb
	.align	2
.LEFDE114:
.LSFDE116:
	.4byte	.LEFDE116-.LASFDE116
.LASFDE116:
	.4byte	.Lframe0
	.4byte	.LFB274
	.4byte	.LFE274-.LFB274
	.byte	0x4
	.4byte	.LCFI64-.LFB274
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
	.4byte	.LCFI65-.LCFI64
	.byte	0xa
	.byte	0xe
	.uleb128 0x1c
	.byte	0x4
	.4byte	.LCFI66-.LCFI65
	.byte	0xb
	.align	2
.LEFDE116:
.LSFDE118:
	.4byte	.LEFDE118-.LASFDE118
.LASFDE118:
	.4byte	.Lframe0
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.byte	0x4
	.4byte	.LCFI67-.LFB190
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
.LEFDE118:
.LSFDE120:
	.4byte	.LEFDE120-.LASFDE120
.LASFDE120:
	.4byte	.Lframe0
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.byte	0x4
	.4byte	.LCFI68-.LFB159
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
	.4byte	.LCFI69-.LCFI68
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI70-.LCFI69
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI71-.LCFI70
	.byte	0xb
	.align	2
.LEFDE120:
.LSFDE122:
	.4byte	.LEFDE122-.LASFDE122
.LASFDE122:
	.4byte	.Lframe0
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.byte	0x4
	.4byte	.LCFI72-.LFB193
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
.LEFDE122:
.LSFDE124:
	.4byte	.LEFDE124-.LASFDE124
.LASFDE124:
	.4byte	.Lframe0
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.byte	0x4
	.4byte	.LCFI73-.LFB205
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
.LEFDE124:
.LSFDE126:
	.4byte	.LEFDE126-.LASFDE126
.LASFDE126:
	.4byte	.Lframe0
	.4byte	.LFB275
	.4byte	.LFE275-.LFB275
	.byte	0x4
	.4byte	.LCFI74-.LFB275
	.byte	0xe
	.uleb128 0xc
	.byte	0x84
	.uleb128 0x3
	.byte	0x85
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI75-.LCFI74
	.byte	0xe
	.uleb128 0x68
	.byte	0x4
	.4byte	.LCFI76-.LCFI75
	.byte	0xa
	.byte	0xe
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI77-.LCFI76
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
	.4byte	.LCFI78-.LFB276
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
	.4byte	.LCFI79-.LFB277
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
	.4byte	.LCFI80-.LFB156
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
	.4byte	.LCFI81-.LFB175
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
	.4byte	.LCFI82-.LFB203
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
	.4byte	.LCFI83-.LFB204
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
	.4byte	.LCFI84-.LCFI83
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI85-.LCFI84
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
	.4byte	.LCFI86-.LFB207
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
	.4byte	.LCFI87-.LFB208
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
	.4byte	.LCFI88-.LFB206
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
	.4byte	.LCFI89-.LCFI88
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI90-.LCFI89
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
	.4byte	.LCFI91-.LFB209
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
	.4byte	.LCFI92-.LFB212
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
	.4byte	.LCFI93-.LFB230
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
	.4byte	.LCFI94-.LCFI93
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
	.4byte	.LCFI95-.LFB241
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
	.4byte	.LCFI96-.LCFI95
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI97-.LCFI96
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI98-.LCFI97
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
	.4byte	.LCFI99-.LFB268
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
	.4byte	.LCFI100-.LCFI99
	.byte	0xe
	.uleb128 0x68
	.byte	0x4
	.4byte	.LCFI101-.LCFI100
	.byte	0xa
	.byte	0xe
	.uleb128 0x10
	.byte	0x4
	.4byte	.LCFI102-.LCFI101
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
	.4byte	.LCFI103-.LFB240
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
	.4byte	.LCFI104-.LCFI103
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI105-.LCFI104
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
	.4byte	.LCFI106-.LFB217
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
	.4byte	.LCFI107-.LCFI106
	.byte	0xa
	.byte	0xce
	.byte	0xc6
	.byte	0xc5
	.byte	0xc4
	.byte	0xe
	.uleb128 0
	.byte	0x4
	.4byte	.LCFI108-.LCFI107
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
	.4byte	.LCFI109-.LFB220
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
	.4byte	.LCFI110-.LCFI109
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI111-.LCFI110
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI112-.LCFI111
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
	.4byte	.LCFI113-.LFB225
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
	.4byte	.LCFI114-.LCFI113
	.byte	0xe
	.uleb128 0x40
	.byte	0x4
	.4byte	.LCFI115-.LCFI114
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI116-.LCFI115
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
	.4byte	.LCFI117-.LFB226
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
	.4byte	.LCFI118-.LFB227
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
	.4byte	.LCFI119-.LFB228
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
	.4byte	.LCFI120-.LFB157
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
	.4byte	.LCFI121-.LFB235
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
	.4byte	.LCFI122-.LCFI121
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
	.4byte	.LCFI123-.LFB259
	.byte	0xe
	.uleb128 0x8
	.byte	0x84
	.uleb128 0x2
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.4byte	.LCFI124-.LCFI123
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
	.4byte	.LCFI125-.LFB260
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
	.4byte	.LCFI126-.LCFI125
	.byte	0xe
	.uleb128 0x40
	.byte	0x4
	.4byte	.LCFI127-.LCFI126
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
	.4byte	.LCFI128-.LFB176
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
	.4byte	.LCFI129-.LCFI128
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
	.4byte	.LCFI130-.LFB248
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
	.4byte	.LCFI131-.LCFI130
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI132-.LCFI131
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
	.4byte	.LCFI133-.LFB267
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
	.4byte	.LCFI134-.LFB251
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
	.4byte	.LCFI135-.LFB252
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
	.4byte	.LCFI136-.LFB218
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
	.4byte	.LCFI137-.LCFI136
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI138-.LCFI137
	.byte	0xa
	.byte	0xe
	.uleb128 0x20
	.byte	0x4
	.4byte	.LCFI139-.LCFI138
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
	.4byte	.LCFI140-.LFB211
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
	.4byte	.LCFI141-.LFB231
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
	.4byte	.LCFI142-.LCFI141
	.byte	0xe
	.uleb128 0x58
	.byte	0x4
	.4byte	.LCFI143-.LCFI142
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI144-.LCFI143
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
	.4byte	.LCFI145-.LFB238
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
	.4byte	.LCFI146-.LCFI145
	.byte	0xe
	.uleb128 0x30
	.byte	0x4
	.4byte	.LCFI147-.LCFI146
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI148-.LCFI147
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
	.4byte	.LCFI149-.LFB179
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
	.4byte	.LCFI150-.LFB181
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
	.4byte	.LCFI151-.LFB213
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
	.4byte	.LCFI152-.LCFI151
	.byte	0xe
	.uleb128 0x90
	.byte	0x4
	.4byte	.LCFI153-.LCFI152
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI154-.LCFI153
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
	.4byte	.LCFI155-.LFB215
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
	.4byte	.LCFI156-.LFB214
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
	.4byte	.LCFI157-.LCFI156
	.byte	0xe
	.uleb128 0x88
	.byte	0x4
	.4byte	.LCFI158-.LCFI157
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI159-.LCFI158
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
	.4byte	.LCFI160-.LFB221
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
	.4byte	.LCFI161-.LFB237
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
	.4byte	.LCFI162-.LFB219
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
	.4byte	.LCFI163-.LFB239
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
	.4byte	.LCFI164-.LCFI163
	.byte	0xa
	.byte	0xe
	.uleb128 0x1c
	.byte	0x4
	.4byte	.LCFI165-.LCFI164
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
	.4byte	.LCFI166-.LFB249
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
	.4byte	.LCFI167-.LFB165
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
	.4byte	.LCFI168-.LCFI167
	.byte	0xe
	.uleb128 0x38
	.byte	0x4
	.4byte	.LCFI169-.LCFI168
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
	.4byte	.LCFI170-.LFB166
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
	.4byte	.LCFI171-.LFB162
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
	.4byte	.LCFI172-.LFB163
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
	.4byte	.LCFI173-.LFB168
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
	.4byte	.LCFI174-.LCFI173
	.byte	0xa
	.byte	0xe
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI175-.LCFI174
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
	.4byte	.LCFI176-.LFB258
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
	.4byte	.LCFI177-.LCFI176
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI178-.LCFI177
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
	.4byte	.LCFI179-.LFB261
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
	.4byte	.LCFI180-.LFB233
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
	.4byte	.LCFI181-.LFB263
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
	.4byte	.LCFI182-.LFB262
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
	.4byte	.LCFI183-.LCFI182
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
	.4byte	.LCFI184-.LFB269
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
	.4byte	.LCFI185-.LCFI184
	.byte	0xe
	.uleb128 0x48
	.byte	0x4
	.4byte	.LCFI186-.LCFI185
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI187-.LCFI186
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
	.4byte	.LCFI188-.LFB164
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
	.4byte	.LCFI189-.LCFI188
	.byte	0xe
	.uleb128 0x60
	.byte	0x4
	.4byte	.LCFI190-.LCFI189
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI191-.LCFI190
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
	.4byte	.LCFI192-.LFB167
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
	.4byte	.LCFI193-.LCFI192
	.byte	0xe
	.uleb128 0x68
	.byte	0x4
	.4byte	.LCFI194-.LCFI193
	.byte	0xa
	.byte	0xe
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI195-.LCFI194
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
	.4byte	.LCFI196-.LFB224
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
	.4byte	.LCFI197-.LFB242
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
	.4byte	.LCFI198-.LFB160
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
	.4byte	.LCFI199-.LFB161
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
	.4byte	0x6836
	.2byte	0x4
	.4byte	.Ldebug_abbrev0
	.byte	0x4
	.uleb128 0x1
	.4byte	.LASF663
	.byte	0x1
	.4byte	.LASF664
	.4byte	.LASF665
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
	.4byte	.LASF666
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
	.byte	0x6c
	.4byte	0x30c
	.uleb128 0x9
	.4byte	.LASF329
	.byte	0x3
	.byte	0x6d
	.4byte	0x30c
	.uleb128 0x9
	.4byte	.LASF330
	.byte	0x3
	.byte	0x6e
	.4byte	0x30c
	.uleb128 0x9
	.4byte	.LASF331
	.byte	0x3
	.byte	0x6f
	.4byte	0x30c
	.uleb128 0x1e
	.4byte	.LASF332
	.byte	0x3
	.2byte	0x3c0
	.4byte	0x64
	.uleb128 0x5
	.byte	0x3
	.4byte	power_up_flag
	.uleb128 0x1f
	.4byte	.LASF336
	.byte	0x3
	.2byte	0x483
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_discard_updated
	.uleb128 0x1e
	.4byte	.LASF333
	.byte	0x2
	.2byte	0x38c
	.4byte	0x301
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_power_up_flush
	.uleb128 0x1e
	.4byte	.LASF334
	.byte	0x2
	.2byte	0x3a1
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
	.2byte	0x8df
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
	.4byte	.LLST301
	.uleb128 0x23
	.4byte	.LASF342
	.byte	0x1
	.byte	0xd2
	.4byte	0x25
	.4byte	.LLST302
	.uleb128 0x23
	.4byte	.LASF114
	.byte	0x1
	.byte	0xd2
	.4byte	0x41
	.4byte	.LLST303
	.uleb128 0x24
	.4byte	.LVL1649
	.4byte	0x67d3
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
	.4byte	.LLST177
	.uleb128 0x23
	.4byte	.LASF346
	.byte	0x1
	.byte	0xcd
	.4byte	0x17bf
	.4byte	.LLST178
	.uleb128 0x23
	.4byte	.LASF347
	.byte	0x1
	.byte	0xcd
	.4byte	0x41
	.4byte	.LLST179
	.uleb128 0x24
	.4byte	.LVL830
	.4byte	0x67de
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
	.4byte	.LLST109
	.uleb128 0x2b
	.ascii	"die\000"
	.byte	0x1
	.byte	0xb6
	.4byte	0x99
	.4byte	.LLST110
	.uleb128 0x2c
	.4byte	.LASF114
	.byte	0x1
	.byte	0xb8
	.4byte	0x83
	.4byte	.LLST111
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x1
	.byte	0xb8
	.4byte	0x83
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB361
	.4byte	.Ldebug_ranges0+0x160
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
	.4byte	.Ldebug_ranges0+0x160
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x160
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL485
	.4byte	0x67e7
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x32
	.4byte	0x17c6
	.4byte	.LBB370
	.4byte	.LBE370-.LBB370
	.byte	0x1
	.byte	0xbd
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB371
	.4byte	.LBE371-.LBB371
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB372
	.4byte	.LBE372-.LBB372
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL489
	.4byte	0x67e7
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
	.4byte	.LLST94
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x85
	.4byte	0x99
	.4byte	.LLST95
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x85
	.4byte	0x99
	.4byte	.LLST96
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
	.4byte	.LVL413
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
	.4byte	.LLST89
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST90
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST91
	.uleb128 0x23
	.4byte	.LASF361
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST92
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
	.4byte	.LLST93
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
	.4byte	__func__.7540
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0xe0
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
	.4byte	.LVL397
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL399
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL400
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL401
	.4byte	0x67f2
	.byte	0
	.uleb128 0x31
	.4byte	.LVL391
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL392
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
	.4byte	.LLST82
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x2b
	.4byte	0x99
	.4byte	.LLST83
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x2b
	.4byte	0x99
	.4byte	.LLST84
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
	.4byte	__func__.7523
	.uleb128 0x31
	.4byte	.LVL361
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL364
	.4byte	0x1ba6
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF667
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
	.2byte	0x2f4
	.4byte	0x99
	.4byte	.LFB270
	.4byte	.LFE270-.LFB270
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c72
	.uleb128 0x24
	.4byte	.LVL1388
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
	.2byte	0x272
	.uleb128 0x40
	.4byte	.LASF382
	.byte	0x5
	.2byte	0x275
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
	.4byte	.LLST81
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
	.4byte	.LVL347
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL348
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL349
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
	.4byte	.LLST80
	.uleb128 0x31
	.4byte	.LVL344
	.4byte	0x67f2
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
	.4byte	.LLST255
	.uleb128 0x31
	.4byte	.LVL1289
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1290
	.4byte	0x22cb
	.uleb128 0x31
	.4byte	.LVL1291
	.4byte	0x20c4
	.uleb128 0x31
	.4byte	.LVL1292
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1293
	.4byte	0x4954
	.uleb128 0x31
	.4byte	.LVL1294
	.4byte	0x2093
	.uleb128 0x31
	.4byte	.LVL1296
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
	.4byte	.LVL1297
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1298
	.4byte	0x20c4
	.uleb128 0x31
	.4byte	.LVL1299
	.4byte	0x2505
	.uleb128 0x31
	.4byte	.LVL1300
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1301
	.4byte	0x373d
	.uleb128 0x31
	.4byte	.LVL1302
	.4byte	0x3598
	.uleb128 0x24
	.4byte	.LVL1303
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
	.4byte	.LLST168
	.uleb128 0x23
	.4byte	.LASF397
	.byte	0x5
	.byte	0xd2
	.4byte	0x30c
	.4byte	.LLST169
	.uleb128 0x2c
	.4byte	.LASF378
	.byte	0x5
	.byte	0xd4
	.4byte	0x301
	.4byte	.LLST170
	.uleb128 0x2c
	.4byte	.LASF357
	.byte	0x5
	.byte	0xd5
	.4byte	0x301
	.4byte	.LLST171
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
	.4byte	.LLST172
	.uleb128 0x2c
	.4byte	.LASF170
	.byte	0x5
	.byte	0xd6
	.4byte	0x301
	.4byte	.LLST173
	.uleb128 0x2c
	.4byte	.LASF398
	.byte	0x5
	.byte	0xd7
	.4byte	0x30c
	.4byte	.LLST174
	.uleb128 0x2c
	.4byte	.LASF399
	.byte	0x5
	.byte	0xd8
	.4byte	0x30c
	.4byte	.LLST175
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
	.4byte	.LLST176
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
	.4byte	.L981
	.uleb128 0x31
	.4byte	.LVL790
	.4byte	0x2093
	.uleb128 0x31
	.4byte	.LVL798
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL812
	.4byte	0x454e
	.uleb128 0x31
	.4byte	.LVL820
	.4byte	0x314e
	.uleb128 0x31
	.4byte	.LVL822
	.4byte	0x21ed
	.uleb128 0x31
	.4byte	.LVL824
	.4byte	0x4222
	.uleb128 0x31
	.4byte	.LVL825
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
	.4byte	.LVL784
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL785
	.4byte	0x17c6
	.uleb128 0x24
	.4byte	.LVL786
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
	.4byte	.LLST251
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
	.4byte	.LLST252
	.uleb128 0x2c
	.4byte	.LASF407
	.byte	0x5
	.byte	0x73
	.4byte	0x30c
	.4byte	.LLST253
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
	.4byte	.LLST254
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x21e8
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7373
	.uleb128 0x31
	.4byte	.LVL1257
	.4byte	0x1f78
	.uleb128 0x31
	.4byte	.LVL1259
	.4byte	0x373d
	.uleb128 0x31
	.4byte	.LVL1260
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1261
	.4byte	0x4954
	.uleb128 0x31
	.4byte	.LVL1265
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1266
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1268
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1269
	.4byte	0x41fd
	.uleb128 0x31
	.4byte	.LVL1271
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1272
	.4byte	0x3598
	.uleb128 0x31
	.4byte	.LVL1274
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1275
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1276
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1277
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1278
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1279
	.4byte	0x4222
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
	.4byte	.LLST75
	.uleb128 0x23
	.4byte	.LASF181
	.byte	0x5
	.byte	0x54
	.4byte	0x30c
	.4byte	.LLST76
	.uleb128 0x2b
	.ascii	"lpa\000"
	.byte	0x5
	.byte	0x54
	.4byte	0x30c
	.4byte	.LLST77
	.uleb128 0x2c
	.4byte	.LASF412
	.byte	0x5
	.byte	0x56
	.4byte	0x301
	.4byte	.LLST78
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x5
	.byte	0x57
	.4byte	0x301
	.4byte	.LLST79
	.uleb128 0x31
	.4byte	.LVL332
	.4byte	0x451a
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
	.4byte	.LLST74
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
	.2byte	0xac3
	.4byte	0x64
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x238f
	.uleb128 0x44
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xac3
	.4byte	0x301
	.4byte	.LLST192
	.uleb128 0x3f
	.ascii	"ret\000"
	.byte	0x2
	.2byte	0xac5
	.4byte	0x64
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x239f
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7303
	.uleb128 0x31
	.4byte	.LVL903
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL905
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL907
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
	.2byte	0xaae
	.4byte	0x64
	.byte	0x1
	.4byte	0x23cf
	.uleb128 0x3d
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xaae
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
	.2byte	0xa8e
	.4byte	0x30c
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2460
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa8e
	.4byte	0x12c0
	.4byte	.LLST72
	.uleb128 0x45
	.4byte	.LASF378
	.byte	0x2
	.2byte	0xa90
	.4byte	0x301
	.4byte	.LLST73
	.uleb128 0x1f
	.4byte	.LASF423
	.byte	0x2
	.2byte	0xa91
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x21e8
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7283
	.uleb128 0x31
	.4byte	.LVL317
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL318
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL319
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL322
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF424
	.byte	0x2
	.2byte	0xa76
	.4byte	0x64
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x24f0
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa76
	.4byte	0x12c0
	.4byte	.LLST237
	.uleb128 0x1f
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa78
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x2500
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7276
	.uleb128 0x31
	.4byte	.LVL1189
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1190
	.4byte	0x41fd
	.uleb128 0x31
	.4byte	.LVL1191
	.4byte	0x23a4
	.uleb128 0x31
	.4byte	.LVL1192
	.4byte	0x2505
	.uleb128 0x31
	.4byte	.LVL1193
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1194
	.4byte	0x373d
	.uleb128 0x31
	.4byte	.LVL1195
	.4byte	0x3598
	.uleb128 0x31
	.4byte	.LVL1197
	.4byte	0x4222
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
	.2byte	0xa12
	.4byte	0x64
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x265f
	.uleb128 0x4e
	.ascii	"p\000"
	.byte	0x2
	.2byte	0xa12
	.4byte	0x12c0
	.4byte	.LLST184
	.uleb128 0x45
	.4byte	.LASF426
	.byte	0x2
	.2byte	0xa14
	.4byte	0x301
	.4byte	.LLST185
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0xa15
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF357
	.byte	0x2
	.2byte	0xa15
	.4byte	0x301
	.4byte	.LLST186
	.uleb128 0x3f
	.ascii	"n\000"
	.byte	0x2
	.2byte	0xa15
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0xa15
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF427
	.byte	0x2
	.2byte	0xa16
	.4byte	0x64
	.4byte	.LLST187
	.uleb128 0x45
	.4byte	.LASF428
	.byte	0x2
	.2byte	0xa17
	.4byte	0x301
	.4byte	.LLST188
	.uleb128 0x40
	.4byte	.LASF429
	.byte	0x2
	.2byte	0xa18
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x266f
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7255
	.uleb128 0x4f
	.4byte	0x3419
	.4byte	.LBB452
	.4byte	.Ldebug_ranges0+0x1e8
	.byte	0x2
	.2byte	0xa48
	.4byte	0x25ce
	.uleb128 0x2e
	.4byte	0x3432
	.uleb128 0x2e
	.4byte	0x3426
	.uleb128 0x31
	.4byte	.LVL875
	.4byte	0x5c2e
	.byte	0
	.uleb128 0x31
	.4byte	.LVL851
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL855
	.4byte	0x4037
	.uleb128 0x31
	.4byte	.LVL856
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL857
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL858
	.4byte	0x41fd
	.uleb128 0x31
	.4byte	.LVL859
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL862
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL863
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL864
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL865
	.4byte	0x3419
	.uleb128 0x31
	.4byte	.LVL867
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL869
	.4byte	0x27b4
	.uleb128 0x31
	.4byte	.LVL870
	.4byte	0x4954
	.uleb128 0x31
	.4byte	.LVL872
	.4byte	0x41fd
	.uleb128 0x31
	.4byte	.LVL877
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL880
	.4byte	0x67f2
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
	.2byte	0xa09
	.4byte	0x64
	.byte	0x1
	.4byte	0x2692
	.uleb128 0x3d
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa09
	.4byte	0x301
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF431
	.byte	0x2
	.2byte	0x9c0
	.4byte	0x301
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x273c
	.uleb128 0x1f
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x9c2
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x3e
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x9c3
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x9c4
	.4byte	0x301
	.4byte	.LLST66
	.uleb128 0x45
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x9c5
	.4byte	0x1210
	.4byte	.LLST67
	.uleb128 0x45
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x9c6
	.4byte	0x301
	.4byte	.LLST68
	.uleb128 0x45
	.4byte	.LASF437
	.byte	0x2
	.2byte	0x9c7
	.4byte	0x301
	.4byte	.LLST69
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x9c8
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF114
	.byte	0x2
	.2byte	0x9c9
	.4byte	0x301
	.4byte	.LLST70
	.uleb128 0x31
	.4byte	.LVL294
	.4byte	0x273c
	.uleb128 0x31
	.4byte	.LVL301
	.4byte	0x278b
	.uleb128 0x31
	.4byte	.LVL307
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF438
	.byte	0x2
	.2byte	0x9ab
	.4byte	0x301
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x278b
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x9ab
	.4byte	0x301
	.4byte	.LLST61
	.uleb128 0x1f
	.4byte	.LASF439
	.byte	0x2
	.2byte	0x9ad
	.4byte	0x301
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x3e
	.4byte	.LASF392
	.byte	0x2
	.2byte	0x9ae
	.4byte	0x301
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x9af
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF440
	.byte	0x2
	.2byte	0x9a2
	.4byte	0x301
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x27b4
	.uleb128 0x1f
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x9a4
	.4byte	0x301
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF441
	.byte	0x2
	.2byte	0x992
	.4byte	0x64
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2815
	.uleb128 0x44
	.4byte	.LASF405
	.byte	0x2
	.2byte	0x992
	.4byte	0x301
	.4byte	.LLST60
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x994
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x994
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF165
	.byte	0x2
	.2byte	0x995
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL200
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL201
	.4byte	0x4ab7
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF442
	.byte	0x2
	.2byte	0x933
	.4byte	0x64
	.byte	0x1
	.4byte	0x283f
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x935
	.4byte	0x64
	.uleb128 0x28
	.uleb128 0x3e
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x95a
	.4byte	0x12c0
	.byte	0
	.byte	0
	.uleb128 0x50
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x914
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x28aa
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x916
	.4byte	0x64
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x917
	.4byte	0x30c
	.uleb128 0x2
	.byte	0x91
	.sleb128 -28
	.uleb128 0x3e
	.4byte	.LASF444
	.byte	0x2
	.2byte	0x918
	.4byte	0x1d3f
	.uleb128 0x31
	.4byte	.LVL651
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL653
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL654
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL655
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL656
	.4byte	0x67f2
	.byte	0
	.uleb128 0x50
	.4byte	.LASF445
	.byte	0x2
	.2byte	0x8e0
	.4byte	.LFB240
	.4byte	.LFE240-.LFB240
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x29c3
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8e2
	.4byte	0x301
	.uleb128 0x52
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x8e3
	.4byte	0x30c
	.4byte	.LLST149
	.uleb128 0x1f
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x8e4
	.4byte	0x30c
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x3f
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x8e5
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x8e6
	.4byte	0x301
	.4byte	.LLST150
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x29d3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7170
	.uleb128 0x53
	.4byte	.LBB413
	.4byte	.LBE413-.LBB413
	.4byte	0x293b
	.uleb128 0x45
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x903
	.4byte	0x301
	.4byte	.LLST151
	.uleb128 0x31
	.4byte	.LVL671
	.4byte	0x67f2
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB410
	.4byte	.LBE410-.LBB410
	.byte	0x2
	.2byte	0x8e9
	.4byte	0x2995
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB411
	.4byte	.LBE411-.LBB411
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB412
	.4byte	.LBE412-.LBB412
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL667
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL666
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL672
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL674
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL675
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL677
	.4byte	0x67f2
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x29d3
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xd
	.byte	0
	.uleb128 0x4
	.4byte	0x29c3
	.uleb128 0x50
	.4byte	.LASF448
	.byte	0x2
	.2byte	0x8c1
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2a7f
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8c3
	.4byte	0x301
	.4byte	.LLST235
	.uleb128 0x52
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x8c4
	.4byte	0x30c
	.4byte	.LLST236
	.uleb128 0x1f
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x8c5
	.4byte	0x30c
	.uleb128 0x2
	.byte	0x91
	.sleb128 -36
	.uleb128 0x3f
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x8c6
	.4byte	0x301
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x2a8f
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7154
	.uleb128 0x31
	.4byte	.LVL1175
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1176
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1180
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1181
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1183
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1184
	.4byte	0x23a4
	.uleb128 0x31
	.4byte	.LVL1185
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1186
	.4byte	0x3598
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2a8f
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x14
	.byte	0
	.uleb128 0x4
	.4byte	0x2a7f
	.uleb128 0x50
	.4byte	.LASF449
	.byte	0x2
	.2byte	0x89c
	.4byte	.LFB238
	.4byte	.LFE238-.LFB238
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b09
	.uleb128 0x44
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x89c
	.4byte	0x12c0
	.4byte	.LLST213
	.uleb128 0x1f
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x89e
	.4byte	0x32d
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x89f
	.4byte	0x1d3f
	.uleb128 0x3e
	.4byte	.LASF451
	.byte	0x2
	.2byte	0x8a0
	.4byte	0x64
	.uleb128 0x3e
	.4byte	.LASF174
	.byte	0x2
	.2byte	0x8a1
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL1054
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL1055
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1056
	.4byte	0x232e
	.byte	0
	.uleb128 0x55
	.4byte	.LASF452
	.byte	0x2
	.2byte	0x882
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b83
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB510
	.4byte	.LBE510-.LBB510
	.byte	0x2
	.2byte	0x886
	.4byte	0x2b79
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB511
	.4byte	.LBE511-.LBB511
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB512
	.4byte	.LBE512-.LBB512
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL1157
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1156
	.4byte	0x378a
	.byte	0
	.uleb128 0x46
	.4byte	.LASF454
	.byte	0x2
	.2byte	0x86e
	.byte	0x1
	.uleb128 0x4d
	.4byte	.LASF455
	.byte	0x2
	.2byte	0x843
	.4byte	0x64
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2c40
	.uleb128 0x45
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x845
	.4byte	0x301
	.4byte	.LLST165
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x845
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x845
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF165
	.byte	0x2
	.2byte	0x845
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF456
	.byte	0x2
	.2byte	0x846
	.4byte	0x301
	.4byte	.LLST166
	.uleb128 0x45
	.4byte	.LASF457
	.byte	0x2
	.2byte	0x846
	.4byte	0x301
	.4byte	.LLST167
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x2c50
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7129
	.uleb128 0x31
	.4byte	.LVL770
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL772
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL773
	.4byte	0x4ab7
	.uleb128 0x31
	.4byte	.LVL777
	.4byte	0x4222
	.uleb128 0x31
	.4byte	.LVL780
	.4byte	0x41fd
	.uleb128 0x31
	.4byte	.LVL782
	.4byte	0x67f2
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2c50
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x11
	.byte	0
	.uleb128 0x4
	.4byte	0x2c40
	.uleb128 0x3b
	.4byte	.LASF458
	.byte	0x2
	.2byte	0x82d
	.4byte	0x64
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2cc0
	.uleb128 0x4e
	.ascii	"p\000"
	.byte	0x2
	.2byte	0x82d
	.4byte	0x12c0
	.4byte	.LLST59
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x82f
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x82f
	.4byte	0x301
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7108
	.uleb128 0x31
	.4byte	.LVL193
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL195
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL196
	.4byte	0x4ab7
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF459
	.byte	0x2
	.2byte	0x820
	.4byte	0x64
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2d11
	.uleb128 0x31
	.4byte	.LVL1283
	.4byte	0x2d37
	.uleb128 0x31
	.4byte	.LVL1284
	.4byte	0x2d11
	.uleb128 0x31
	.4byte	.LVL1285
	.4byte	0x2d37
	.uleb128 0x31
	.4byte	.LVL1286
	.4byte	0x2d11
	.uleb128 0x31
	.4byte	.LVL1287
	.4byte	0x1f6f
	.uleb128 0x31
	.4byte	.LVL1288
	.4byte	0x232e
	.byte	0
	.uleb128 0x56
	.4byte	.LASF495
	.byte	0x2
	.2byte	0x80d
	.byte	0x1
	.4byte	0x2d37
	.uleb128 0x3d
	.4byte	.LASF422
	.byte	0x2
	.2byte	0x80d
	.4byte	0x12c0
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x80f
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF460
	.byte	0x2
	.2byte	0x6a8
	.4byte	0x64
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3082
	.uleb128 0x44
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x6a8
	.4byte	0x12c0
	.4byte	.LLST197
	.uleb128 0x45
	.4byte	.LASF461
	.byte	0x2
	.2byte	0x6aa
	.4byte	0x301
	.4byte	.LLST198
	.uleb128 0x45
	.4byte	.LASF462
	.byte	0x2
	.2byte	0x6aa
	.4byte	0x301
	.4byte	.LLST199
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF172
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x301
	.4byte	.LLST200
	.uleb128 0x3e
	.4byte	.LASF170
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF463
	.byte	0x2
	.2byte	0x6ac
	.4byte	0x301
	.4byte	.LLST201
	.uleb128 0x45
	.4byte	.LASF464
	.byte	0x2
	.2byte	0x6ac
	.4byte	0x301
	.4byte	.LLST202
	.uleb128 0x3e
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x6ad
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF465
	.byte	0x2
	.2byte	0x6ae
	.4byte	0x64
	.4byte	.LLST203
	.uleb128 0x3e
	.4byte	.LASF466
	.byte	0x2
	.2byte	0x6ae
	.4byte	0x64
	.uleb128 0x3e
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x6af
	.4byte	0x301
	.uleb128 0x52
	.ascii	"n\000"
	.byte	0x2
	.2byte	0x6af
	.4byte	0x301
	.4byte	.LLST204
	.uleb128 0x3f
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x6af
	.4byte	0x301
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30c
	.4byte	.LLST205
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30c
	.4byte	.LLST206
	.uleb128 0x45
	.4byte	.LASF406
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30c
	.4byte	.LLST207
	.uleb128 0x45
	.4byte	.LASF398
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30c
	.4byte	.LLST208
	.uleb128 0x45
	.4byte	.LASF412
	.byte	0x2
	.2byte	0x6b1
	.4byte	0x301
	.4byte	.LLST209
	.uleb128 0x3e
	.4byte	.LASF467
	.byte	0x2
	.2byte	0x6b1
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF468
	.byte	0x2
	.2byte	0x6b2
	.4byte	0x30c
	.4byte	.LLST210
	.uleb128 0x3e
	.4byte	.LASF469
	.byte	0x2
	.2byte	0x6b3
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x6b4
	.4byte	0x1d3f
	.uleb128 0x3e
	.4byte	.LASF470
	.byte	0x2
	.2byte	0x6b5
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x3092
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7051
	.uleb128 0x48
	.4byte	.LASF472
	.byte	0x2
	.2byte	0x744
	.4byte	.L1140
	.uleb128 0x48
	.4byte	.LASF402
	.byte	0x2
	.2byte	0x74c
	.4byte	.L1152
	.uleb128 0x48
	.4byte	.LASF473
	.byte	0x2
	.2byte	0x7ff
	.4byte	.L1160
	.uleb128 0x40
	.4byte	.LASF474
	.byte	0x2
	.2byte	0x809
	.uleb128 0x53
	.4byte	.LBB473
	.4byte	.LBE473-.LBB473
	.4byte	0x2f11
	.uleb128 0x3e
	.4byte	.LASF475
	.byte	0x2
	.2byte	0x71c
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF476
	.byte	0x2
	.2byte	0x71d
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL971
	.4byte	0x1af8
	.byte	0
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x230
	.4byte	0x2f83
	.uleb128 0x45
	.4byte	.LASF477
	.byte	0x2
	.2byte	0x777
	.4byte	0x30c
	.4byte	.LLST211
	.uleb128 0x45
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x778
	.4byte	0x30c
	.4byte	.LLST212
	.uleb128 0x31
	.4byte	.LVL1013
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1016
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1021
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1023
	.4byte	0x44d7
	.uleb128 0x31
	.4byte	.LVL1025
	.4byte	0x3097
	.uleb128 0x31
	.4byte	.LVL1028
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1030
	.4byte	0x44d7
	.uleb128 0x31
	.4byte	.LVL1031
	.4byte	0x44d7
	.byte	0
	.uleb128 0x53
	.4byte	.LBB477
	.4byte	.LBE477-.LBB477
	.4byte	0x2fc4
	.uleb128 0x3e
	.4byte	.LASF479
	.byte	0x2
	.2byte	0x7b6
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x7b7
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL1036
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1037
	.4byte	0x44d7
	.uleb128 0x31
	.4byte	.LVL1038
	.4byte	0x3939
	.byte	0
	.uleb128 0x31
	.4byte	.LVL932
	.4byte	0x443f
	.uleb128 0x31
	.4byte	.LVL934
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL939
	.4byte	0x454e
	.uleb128 0x31
	.4byte	.LVL947
	.4byte	0x314e
	.uleb128 0x31
	.4byte	.LVL952
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL954
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL958
	.4byte	0x44d7
	.uleb128 0x31
	.4byte	.LVL974
	.4byte	0x36ab
	.uleb128 0x31
	.4byte	.LVL979
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL988
	.4byte	0x454e
	.uleb128 0x31
	.4byte	.LVL998
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1003
	.4byte	0x44d7
	.uleb128 0x31
	.4byte	.LVL1008
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1011
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1012
	.4byte	0x44d7
	.uleb128 0x31
	.4byte	.LVL1017
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1019
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1034
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1035
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1040
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1043
	.4byte	0x232e
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3092
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x15
	.byte	0
	.uleb128 0x4
	.4byte	0x3082
	.uleb128 0x50
	.4byte	.LASF480
	.byte	0x2
	.2byte	0x68c
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x314e
	.uleb128 0x4e
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x68c
	.4byte	0x30c
	.4byte	.LLST142
	.uleb128 0x44
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x68c
	.4byte	0x30c
	.4byte	.LLST143
	.uleb128 0x45
	.4byte	.LASF412
	.byte	0x2
	.2byte	0x68e
	.4byte	0x301
	.4byte	.LLST144
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7017
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x198
	.4byte	0x313b
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x692
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF392
	.byte	0x2
	.2byte	0x693
	.4byte	0x301
	.4byte	.LLST145
	.uleb128 0x45
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x694
	.4byte	0x301
	.4byte	.LLST146
	.uleb128 0x31
	.4byte	.LVL641
	.4byte	0x4097
	.uleb128 0x31
	.4byte	.LVL642
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL643
	.4byte	0x41fd
	.byte	0
	.uleb128 0x31
	.4byte	.LVL636
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL648
	.4byte	0x3939
	.byte	0
	.uleb128 0x50
	.4byte	.LASF481
	.byte	0x2
	.2byte	0x678
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x319f
	.uleb128 0x57
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x678
	.4byte	0x12c0
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x44
	.4byte	.LASF170
	.byte	0x2
	.2byte	0x678
	.4byte	0x301
	.4byte	.LLST57
	.uleb128 0x44
	.4byte	.LASF172
	.byte	0x2
	.2byte	0x678
	.4byte	0x301
	.4byte	.LLST58
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x67a
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF482
	.byte	0x2
	.2byte	0x66f
	.4byte	0x64
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31cc
	.uleb128 0x31
	.4byte	.LVL759
	.4byte	0x31cc
	.uleb128 0x31
	.4byte	.LVL760
	.4byte	0x3225
	.byte	0
	.uleb128 0x50
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x658
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3201
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x65a
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL757
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL758
	.4byte	0x17c6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF484
	.byte	0x2
	.2byte	0x648
	.4byte	0x64
	.4byte	.LFB226
	.4byte	.LFE226-.LFB226
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3225
	.uleb128 0x31
	.4byte	.LVL756
	.4byte	0x3225
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF485
	.byte	0x2
	.2byte	0x5ea
	.4byte	0x64
	.4byte	.LFB225
	.4byte	.LFE225-.LFB225
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3333
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x5ea
	.4byte	0x3333
	.4byte	.LLST158
	.uleb128 0x45
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x5ec
	.4byte	0x322
	.4byte	.LLST159
	.uleb128 0x3e
	.4byte	.LASF85
	.byte	0x2
	.2byte	0x5ec
	.4byte	0x322
	.uleb128 0x45
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x5ed
	.4byte	0x322
	.4byte	.LLST160
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x5ee
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x5ef
	.4byte	0x539
	.4byte	.LLST161
	.uleb128 0x45
	.4byte	.LASF98
	.byte	0x2
	.2byte	0x5f0
	.4byte	0x630
	.4byte	.LLST162
	.uleb128 0x45
	.4byte	.LASF99
	.byte	0x2
	.2byte	0x5f1
	.4byte	0x539
	.4byte	.LLST163
	.uleb128 0x1f
	.4byte	.LASF95
	.byte	0x2
	.2byte	0x5f2
	.4byte	0x301
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x45
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x5f3
	.4byte	0x301
	.4byte	.LLST164
	.uleb128 0x3e
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x5f4
	.4byte	0x3339
	.uleb128 0x31
	.4byte	.LVL721
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL729
	.4byte	0x443f
	.uleb128 0x31
	.4byte	.LVL736
	.4byte	0x3ddf
	.uleb128 0x31
	.4byte	.LVL737
	.4byte	0x3d87
	.uleb128 0x31
	.4byte	.LVL738
	.4byte	0x3cbc
	.uleb128 0x31
	.4byte	.LVL739
	.4byte	0x3cbc
	.uleb128 0x31
	.4byte	.LVL741
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL747
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL750
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
	.2byte	0x580
	.4byte	0x64
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x33ee
	.uleb128 0x45
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x582
	.4byte	0x322
	.4byte	.LLST297
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6954
	.uleb128 0x31
	.4byte	.LVL1562
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1565
	.4byte	0x443f
	.uleb128 0x31
	.4byte	.LVL1567
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1568
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1569
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1570
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1571
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1574
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1575
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1576
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1577
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1578
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1580
	.4byte	0x1af8
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF493
	.byte	0x2
	.2byte	0x57b
	.4byte	0x64
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3419
	.uleb128 0x44
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x57b
	.4byte	0x301
	.4byte	.LLST56
	.byte	0
	.uleb128 0x56
	.4byte	.LASF496
	.byte	0x2
	.2byte	0x573
	.byte	0x1
	.4byte	0x343f
	.uleb128 0x3d
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x573
	.4byte	0x301
	.uleb128 0x3d
	.4byte	.LASF372
	.byte	0x2
	.2byte	0x573
	.4byte	0x64
	.byte	0
	.uleb128 0x43
	.4byte	.LASF498
	.byte	0x2
	.2byte	0x566
	.4byte	0x64
	.byte	0x1
	.uleb128 0x3b
	.4byte	.LASF499
	.byte	0x2
	.2byte	0x477
	.4byte	0x64
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3598
	.uleb128 0x3e
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x479
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF500
	.byte	0x2
	.2byte	0x479
	.4byte	0x301
	.4byte	.LLST155
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x47a
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x47a
	.4byte	0x301
	.4byte	.LLST156
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x47b
	.4byte	0x322
	.uleb128 0x3f
	.ascii	"j\000"
	.byte	0x2
	.2byte	0x47b
	.4byte	0x322
	.uleb128 0x45
	.4byte	.LASF501
	.byte	0x2
	.2byte	0x47c
	.4byte	0x3339
	.4byte	.LLST157
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x29d3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6897
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB419
	.4byte	.LBE419-.LBB419
	.byte	0x2
	.2byte	0x484
	.4byte	0x3522
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB420
	.4byte	.LBE420-.LBB420
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB421
	.4byte	.LBE421-.LBB421
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL688
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL689
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL690
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL691
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL695
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL696
	.4byte	0x4ab7
	.uleb128 0x31
	.4byte	.LVL703
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL704
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL705
	.4byte	0x436d
	.uleb128 0x31
	.4byte	.LVL706
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL707
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL708
	.4byte	0x436d
	.uleb128 0x31
	.4byte	.LVL709
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF502
	.byte	0x2
	.2byte	0x41c
	.4byte	0x64
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3659
	.uleb128 0x45
	.4byte	.LASF86
	.byte	0x2
	.2byte	0x41e
	.4byte	0x301
	.4byte	.LLST233
	.uleb128 0x3e
	.4byte	.LASF503
	.byte	0x2
	.2byte	0x41f
	.4byte	0x3659
	.uleb128 0x45
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x420
	.4byte	0x301
	.4byte	.LLST234
	.uleb128 0x40
	.4byte	.LASF505
	.byte	0x2
	.2byte	0x44d
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6875
	.uleb128 0x31
	.4byte	.LVL1160
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1161
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1162
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1163
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1164
	.4byte	0x3719
	.uleb128 0x31
	.4byte	.LVL1165
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1166
	.4byte	0x4247
	.uleb128 0x31
	.4byte	.LVL1167
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1168
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1171
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1172
	.4byte	0x436d
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x810
	.uleb128 0x56
	.4byte	.LASF506
	.byte	0x2
	.2byte	0x3e3
	.byte	0x1
	.4byte	0x36ab
	.uleb128 0x28
	.uleb128 0x3f
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x3e8
	.4byte	0x30c
	.uleb128 0x3f
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x3e9
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF507
	.byte	0x2
	.2byte	0x3ea
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x3eb
	.4byte	0x32d
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x3ec
	.4byte	0x1d3f
	.byte	0
	.byte	0
	.uleb128 0x50
	.4byte	.LASF508
	.byte	0x2
	.2byte	0x3b1
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3719
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x3b1
	.4byte	0x3333
	.4byte	.LLST152
	.uleb128 0x52
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x3b3
	.4byte	0x30c
	.4byte	.LLST153
	.uleb128 0x3e
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x3b4
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x3b5
	.4byte	0x539
	.4byte	.LLST154
	.uleb128 0x31
	.4byte	.LVL682
	.4byte	0x1af8
	.uleb128 0x24
	.4byte	.LVL684
	.4byte	0x3ad6
	.uleb128 0x31
	.4byte	.LVL686
	.4byte	0x17c6
	.byte	0
	.uleb128 0x56
	.4byte	.LASF509
	.byte	0x2
	.2byte	0x3a2
	.byte	0x1
	.4byte	0x373d
	.uleb128 0x3d
	.4byte	.LASF183
	.byte	0x2
	.2byte	0x3a2
	.4byte	0x30c
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x3a4
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF510
	.byte	0x2
	.2byte	0x38d
	.4byte	0x64
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x378a
	.uleb128 0x44
	.4byte	.LASF511
	.byte	0x2
	.2byte	0x38d
	.4byte	0x30c
	.4byte	.LLST225
	.uleb128 0x45
	.4byte	.LASF512
	.byte	0x2
	.2byte	0x38f
	.4byte	0x30c
	.4byte	.LLST226
	.uleb128 0x31
	.4byte	.LVL1135
	.4byte	0x3860
	.uleb128 0x31
	.4byte	.LVL1136
	.4byte	0x2b83
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x367
	.4byte	0x30c
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x385a
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x367
	.4byte	0x30c
	.4byte	.LLST227
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x367
	.4byte	0x30c
	.4byte	.LLST228
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x367
	.4byte	0x385a
	.4byte	.LLST229
	.uleb128 0x52
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x369
	.4byte	0x301
	.4byte	.LLST230
	.uleb128 0x3e
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x369
	.4byte	0x301
	.uleb128 0x52
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x36a
	.4byte	0x30c
	.4byte	.LLST231
	.uleb128 0x1f
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x36b
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x51
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x36c
	.4byte	0x32d
	.uleb128 0x3
	.byte	0x91
	.sleb128 -124
	.uleb128 0x45
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x36d
	.4byte	0x30c
	.4byte	.LLST232
	.uleb128 0x1f
	.4byte	.LASF353
	.byte	0x2
	.2byte	0x36e
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -104
	.uleb128 0x31
	.4byte	.LVL1148
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1149
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1151
	.4byte	0x17c6
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x2f6
	.uleb128 0x4d
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x342
	.4byte	0x30c
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3939
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x342
	.4byte	0x30c
	.4byte	.LLST219
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x342
	.4byte	0x30c
	.4byte	.LLST220
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x342
	.4byte	0x385a
	.4byte	.LLST221
	.uleb128 0x52
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x344
	.4byte	0x301
	.4byte	.LLST222
	.uleb128 0x3e
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x344
	.4byte	0x301
	.uleb128 0x52
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x345
	.4byte	0x30c
	.4byte	.LLST223
	.uleb128 0x1f
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x346
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x51
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x347
	.4byte	0x32d
	.uleb128 0x3
	.byte	0x91
	.sleb128 -124
	.uleb128 0x45
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x348
	.4byte	0x30c
	.4byte	.LLST224
	.uleb128 0x1f
	.4byte	.LASF353
	.byte	0x2
	.2byte	0x349
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -104
	.uleb128 0x31
	.4byte	.LVL1118
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1119
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1120
	.4byte	0x3ad6
	.uleb128 0x31
	.4byte	.LVL1123
	.4byte	0x17c6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF521
	.byte	0x2
	.2byte	0x321
	.4byte	0x64
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x39ef
	.uleb128 0x4e
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x321
	.4byte	0x30c
	.4byte	.LLST138
	.uleb128 0x4e
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x321
	.4byte	0x539
	.4byte	.LLST139
	.uleb128 0x44
	.4byte	.LASF522
	.byte	0x2
	.2byte	0x321
	.4byte	0x64
	.4byte	.LLST140
	.uleb128 0x45
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x323
	.4byte	0x301
	.4byte	.LLST141
	.uleb128 0x1f
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x323
	.4byte	0x301
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x1f
	.4byte	.LASF525
	.byte	0x2
	.2byte	0x323
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x39ff
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6802
	.uleb128 0x48
	.4byte	.LASF526
	.byte	0x2
	.2byte	0x32c
	.4byte	.L745
	.uleb128 0x31
	.4byte	.LVL623
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL625
	.4byte	0x3a3f
	.uleb128 0x31
	.4byte	.LVL626
	.4byte	0x3a96
	.uleb128 0x31
	.4byte	.LVL627
	.4byte	0x3e90
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x39ff
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x8
	.byte	0
	.uleb128 0x4
	.4byte	0x39ef
	.uleb128 0x3b
	.4byte	.LASF527
	.byte	0x2
	.2byte	0x314
	.4byte	0x64
	.4byte	.LFB211
	.4byte	.LFE211-.LFB211
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a3f
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x316
	.4byte	0x301
	.4byte	.LLST196
	.uleb128 0x31
	.4byte	.LVL919
	.4byte	0x365f
	.uleb128 0x31
	.4byte	.LVL922
	.4byte	0x3a96
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF528
	.byte	0x2
	.2byte	0x2ec
	.4byte	0x301
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a96
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x2ee
	.4byte	0x301
	.uleb128 0x1f
	.4byte	.LASF529
	.byte	0x2
	.2byte	0x2ef
	.4byte	0x301
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x3e
	.4byte	.LASF530
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x3092
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6785
	.uleb128 0x31
	.4byte	.LVL174
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF531
	.byte	0x2
	.2byte	0x2e2
	.4byte	0x64
	.4byte	.LFB209
	.4byte	.LFE209-.LFB209
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ad6
	.uleb128 0x44
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x2e2
	.4byte	0x301
	.4byte	.LLST137
	.uleb128 0x3e
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x2e4
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL618
	.4byte	0x3ad6
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF532
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x64
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3bec
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x3333
	.4byte	.LLST130
	.uleb128 0x44
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x30c
	.4byte	.LLST131
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x539
	.4byte	.LLST132
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x2a9
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x2aa
	.4byte	0x3339
	.4byte	.LLST133
	.uleb128 0x45
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x2ab
	.4byte	0x301
	.4byte	.LLST134
	.uleb128 0x40
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x2ad
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6760
	.uleb128 0x4f
	.4byte	0x17c6
	.4byte	.LBB385
	.4byte	.Ldebug_ranges0+0x180
	.byte	0x2
	.2byte	0x2c3
	.4byte	0x3bb5
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x180
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x180
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL592
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL589
	.4byte	0x3bec
	.uleb128 0x31
	.4byte	.LVL590
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL591
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL595
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL598
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL599
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF534
	.byte	0x2
	.2byte	0x277
	.4byte	0x64
	.4byte	.LFB207
	.4byte	.LFE207-.LFB207
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ca7
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x277
	.4byte	0x3333
	.4byte	.LLST126
	.uleb128 0x3e
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x279
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x279
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF535
	.byte	0x2
	.2byte	0x27a
	.4byte	0x301
	.4byte	.LLST127
	.uleb128 0x45
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x27b
	.4byte	0x630
	.4byte	.LLST128
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x27c
	.4byte	0x539
	.4byte	.LLST129
	.uleb128 0x3e
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x27d
	.4byte	0x3339
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x3cb7
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6746
	.uleb128 0x31
	.4byte	.LVL572
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL573
	.4byte	0x4247
	.uleb128 0x31
	.4byte	.LVL579
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL582
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL583
	.4byte	0x3cbc
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3cb7
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1e
	.byte	0
	.uleb128 0x4
	.4byte	0x3ca7
	.uleb128 0x3b
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x249
	.4byte	0x64
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3d87
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x249
	.4byte	0x3333
	.4byte	.LLST135
	.uleb128 0x1f
	.4byte	.LASF538
	.byte	0x2
	.2byte	0x24b
	.4byte	0x301
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x3e
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x24c
	.4byte	0x301
	.uleb128 0x1f
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x24d
	.4byte	0x301
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x45
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x24e
	.4byte	0x630
	.4byte	.LLST136
	.uleb128 0x1f
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x24f
	.4byte	0x539
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x3e
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x250
	.4byte	0x3339
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6732
	.uleb128 0x31
	.4byte	.LVL605
	.4byte	0x3ddf
	.uleb128 0x31
	.4byte	.LVL607
	.4byte	0x3d87
	.uleb128 0x31
	.4byte	.LVL609
	.4byte	0x436d
	.uleb128 0x31
	.4byte	.LVL611
	.4byte	0x3d87
	.uleb128 0x31
	.4byte	.LVL613
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL614
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL615
	.4byte	0x3ad6
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF540
	.byte	0x2
	.2byte	0x231
	.4byte	0x64
	.byte	0x1
	.4byte	0x3dca
	.uleb128 0x3d
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x231
	.4byte	0x3333
	.uleb128 0x3e
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x233
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x234
	.4byte	0x630
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x3dda
	.4byte	.LASF540
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3dda
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x19
	.byte	0
	.uleb128 0x4
	.4byte	0x3dca
	.uleb128 0x3b
	.4byte	.LASF542
	.byte	0x2
	.2byte	0x209
	.4byte	0x64
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3e90
	.uleb128 0x44
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x209
	.4byte	0x3333
	.4byte	.LLST121
	.uleb128 0x1f
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x20b
	.4byte	0x630
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x45
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x20c
	.4byte	0x539
	.4byte	.LLST122
	.uleb128 0x1f
	.4byte	.LASF543
	.byte	0x2
	.2byte	0x20d
	.4byte	0x630
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x45
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x20e
	.4byte	0x301
	.4byte	.LLST123
	.uleb128 0x3e
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x20e
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x20f
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF544
	.byte	0x2
	.2byte	0x210
	.4byte	0x301
	.4byte	.LLST124
	.uleb128 0x45
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x211
	.4byte	0x301
	.4byte	.LLST125
	.uleb128 0x31
	.4byte	.LVL560
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL569
	.4byte	0x436d
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF545
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x64
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3f37
	.uleb128 0x44
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x301
	.4byte	.LLST118
	.uleb128 0x44
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x301
	.4byte	.LLST119
	.uleb128 0x45
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x1e9
	.4byte	0x3339
	.4byte	.LLST120
	.uleb128 0x1f
	.4byte	.LASF546
	.byte	0x2
	.2byte	0x1ea
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x23df
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6688
	.uleb128 0x31
	.4byte	.LVL543
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL545
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL548
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL550
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL551
	.4byte	0x6808
	.uleb128 0x31
	.4byte	.LVL552
	.4byte	0x6808
	.uleb128 0x31
	.4byte	.LVL553
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF547
	.byte	0x2
	.2byte	0x1bf
	.4byte	0x64
	.byte	0x1
	.4byte	0x3fb6
	.uleb128 0x3d
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x1bf
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF548
	.byte	0x2
	.2byte	0x1c1
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF549
	.byte	0x2
	.2byte	0x1c2
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF550
	.byte	0x2
	.2byte	0x1c3
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF551
	.byte	0x2
	.2byte	0x1c5
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF552
	.byte	0x2
	.2byte	0x1c6
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x1c7
	.4byte	0x1210
	.uleb128 0x3e
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x1c7
	.4byte	0x1210
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x3092
	.4byte	.LASF547
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF554
	.byte	0x2
	.2byte	0x1ab
	.4byte	0x64
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ffd
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x1ab
	.4byte	0x301
	.4byte	.LLST46
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x1ad
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x1ae
	.4byte	0x1210
	.4byte	.LLST47
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF555
	.byte	0x2
	.2byte	0x1a6
	.4byte	0x64
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4031
	.uleb128 0x44
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x1a6
	.4byte	0x4031
	.4byte	.LLST45
	.uleb128 0x24
	.4byte	.LVL145
	.4byte	0x4037
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x1210
	.uleb128 0x3b
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x193
	.4byte	0x64
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4097
	.uleb128 0x44
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x193
	.4byte	0x4031
	.4byte	.LLST42
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x193
	.4byte	0x301
	.4byte	.LLST43
	.uleb128 0x3e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x195
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x196
	.4byte	0x1210
	.4byte	.LLST44
	.uleb128 0x31
	.4byte	.LVL139
	.4byte	0x4097
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF558
	.byte	0x2
	.2byte	0x176
	.4byte	0x64
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4104
	.uleb128 0x44
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x176
	.4byte	0x4031
	.4byte	.LLST40
	.uleb128 0x44
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x176
	.4byte	0x301
	.4byte	.LLST41
	.uleb128 0x1f
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x178
	.4byte	0x1210
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x3e
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x178
	.4byte	0x1210
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x4114
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6647
	.uleb128 0x31
	.4byte	.LVL132
	.4byte	0x67f2
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x4114
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x10
	.byte	0
	.uleb128 0x4
	.4byte	0x4104
	.uleb128 0x3b
	.4byte	.LASF560
	.byte	0x2
	.2byte	0x145
	.4byte	0x64
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4188
	.uleb128 0x44
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x145
	.4byte	0x301
	.4byte	.LLST37
	.uleb128 0x1f
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x147
	.4byte	0x1210
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x1f
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x147
	.4byte	0x1210
	.uleb128 0x1
	.byte	0x53
	.uleb128 0x45
	.4byte	.LASF561
	.byte	0x2
	.2byte	0x148
	.4byte	0x301
	.4byte	.LLST38
	.uleb128 0x3e
	.4byte	.LASF562
	.byte	0x2
	.2byte	0x149
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF563
	.byte	0x2
	.2byte	0x14a
	.4byte	0x301
	.byte	0
	.uleb128 0x26
	.4byte	.LASF564
	.byte	0x2
	.byte	0xf8
	.4byte	0x64
	.byte	0x1
	.4byte	0x41fd
	.uleb128 0x34
	.4byte	.LASF432
	.byte	0x2
	.byte	0xf8
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF435
	.byte	0x2
	.byte	0xfa
	.4byte	0x1210
	.uleb128 0x35
	.4byte	.LASF559
	.byte	0x2
	.byte	0xfa
	.4byte	0x1210
	.uleb128 0x35
	.4byte	.LASF565
	.byte	0x2
	.byte	0xfb
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF566
	.byte	0x2
	.byte	0xfc
	.4byte	0x301
	.uleb128 0x35
	.4byte	.LASF550
	.byte	0x2
	.byte	0xfd
	.4byte	0x30c
	.uleb128 0x35
	.4byte	.LASF567
	.byte	0x2
	.byte	0xfe
	.4byte	0x30c
	.uleb128 0x35
	.4byte	.LASF563
	.byte	0x2
	.byte	0xff
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF114
	.byte	0x2
	.2byte	0x100
	.4byte	0x301
	.byte	0
	.uleb128 0x58
	.4byte	.LASF568
	.byte	0x2
	.byte	0xd9
	.byte	0x1
	.4byte	0x4222
	.uleb128 0x34
	.4byte	.LASF432
	.byte	0x2
	.byte	0xd9
	.4byte	0x301
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x4114
	.4byte	.LASF568
	.byte	0
	.uleb128 0x58
	.4byte	.LASF569
	.byte	0x2
	.byte	0xd2
	.byte	0x1
	.4byte	0x4247
	.uleb128 0x34
	.4byte	.LASF432
	.byte	0x2
	.byte	0xd2
	.4byte	0x301
	.uleb128 0x41
	.4byte	.LASF362
	.4byte	0x4114
	.4byte	.LASF569
	.byte	0
	.uleb128 0x22
	.4byte	.LASF570
	.byte	0x2
	.byte	0xb4
	.4byte	0x301
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x42ca
	.uleb128 0x37
	.ascii	"tmp\000"
	.byte	0x2
	.byte	0xb6
	.4byte	0x301
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0xb7
	.4byte	0x42ca
	.uleb128 0x59
	.4byte	.LASF571
	.byte	0x2
	.byte	0xb9
	.4byte	.L518
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x118
	.4byte	0x42a8
	.uleb128 0x35
	.4byte	.LASF572
	.byte	0x2
	.byte	0xbf
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL463
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL464
	.4byte	0x1954
	.byte	0
	.uleb128 0x5a
	.4byte	0x43b4
	.4byte	.LBB315
	.4byte	.Ldebug_ranges0+0x100
	.byte	0x2
	.byte	0xba
	.uleb128 0x31
	.4byte	.LVL465
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL466
	.4byte	0x67f2
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0xadc
	.uleb128 0x22
	.4byte	.LASF573
	.byte	0x2
	.byte	0xa2
	.4byte	0x64
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4329
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x2
	.byte	0xa2
	.4byte	0x301
	.4byte	.LLST27
	.uleb128 0x37
	.ascii	"ret\000"
	.byte	0x2
	.byte	0xa4
	.4byte	0x64
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x37
	.ascii	"i\000"
	.byte	0x2
	.byte	0xa5
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0xa6
	.4byte	0x42ca
	.uleb128 0x5a
	.4byte	0x43a8
	.4byte	.LBB251
	.4byte	.Ldebug_ranges0+0x50
	.byte	0x2
	.byte	0xa8
	.byte	0
	.uleb128 0x49
	.4byte	.LASF574
	.byte	0x2
	.byte	0x75
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x436d
	.uleb128 0x20
	.4byte	.LASF114
	.byte	0x2
	.byte	0x77
	.4byte	0x317
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x2
	.byte	0x78
	.4byte	0x301
	.uleb128 0x4a
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x78
	.4byte	0x301
	.4byte	.LLST26
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x79
	.4byte	0x42ca
	.byte	0
	.uleb128 0x58
	.4byte	.LASF575
	.byte	0x2
	.byte	0x61
	.byte	0x1
	.4byte	0x43a8
	.uleb128 0x27
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x61
	.4byte	0x301
	.uleb128 0x34
	.4byte	.LASF576
	.byte	0x2
	.byte	0x61
	.4byte	0x301
	.uleb128 0x28
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x66
	.4byte	0x42ca
	.uleb128 0x28
	.uleb128 0x35
	.4byte	.LASF572
	.byte	0x2
	.byte	0x68
	.4byte	0x301
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5b
	.4byte	.LASF577
	.byte	0x2
	.byte	0x5c
	.4byte	0x301
	.byte	0x1
	.uleb128 0x5b
	.4byte	.LASF578
	.byte	0x2
	.byte	0x57
	.4byte	0x301
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF579
	.byte	0x2
	.byte	0x4b
	.4byte	0x301
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x443f
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
	.4byte	0x42ca
	.uleb128 0x5c
	.4byte	0x17c6
	.4byte	.LBB242
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
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x22
	.4byte	.LASF580
	.byte	0x2
	.byte	0x2e
	.4byte	0x64
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x44d7
	.uleb128 0x23
	.4byte	.LASF178
	.byte	0x2
	.byte	0x2e
	.4byte	0x30c
	.4byte	.LLST87
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x2
	.byte	0x2e
	.4byte	0x64
	.4byte	.LLST88
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
	.4byte	.LASF581
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
	.4byte	.LASF582
	.byte	0x2
	.byte	0x32
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -88
	.uleb128 0x35
	.4byte	.LASF583
	.byte	0x2
	.byte	0x33
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL380
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL383
	.4byte	0x1af8
	.byte	0
	.uleb128 0x22
	.4byte	.LASF584
	.byte	0x2
	.byte	0x1e
	.4byte	0x30c
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x451a
	.uleb128 0x23
	.4byte	.LASF585
	.byte	0x2
	.byte	0x1e
	.4byte	0x30c
	.4byte	.LLST24
	.uleb128 0x4c
	.4byte	.LASF586
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
	.4byte	.LASF587
	.byte	0x2
	.byte	0x14
	.4byte	0x301
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x454e
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
	.4byte	.LASF588
	.byte	0x2
	.byte	0xc
	.4byte	0x301
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4582
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
	.4byte	.LASF589
	.byte	0x2
	.byte	0x4
	.4byte	0x301
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x45c5
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
	.4byte	.LASF590
	.byte	0x4
	.2byte	0x13c
	.4byte	0x317
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4789
	.uleb128 0x52
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x13e
	.4byte	0x30c
	.4byte	.LLST217
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
	.4byte	.LLST218
	.uleb128 0x3e
	.4byte	.LASF591
	.byte	0x4
	.2byte	0x142
	.4byte	0x4789
	.uleb128 0x40
	.4byte	.LASF592
	.byte	0x4
	.2byte	0x158
	.uleb128 0x4f
	.4byte	0x17c6
	.4byte	.LBB488
	.4byte	.Ldebug_ranges0+0x250
	.byte	0x4
	.2byte	0x15e
	.4byte	0x4671
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x250
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x250
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL1092
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB494
	.4byte	.LBE494-.LBB494
	.byte	0x4
	.2byte	0x15f
	.4byte	0x46cb
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB495
	.4byte	.LBE495-.LBB495
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB496
	.4byte	.LBE496-.LBB496
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL1093
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1079
	.4byte	0x48f1
	.uleb128 0x31
	.4byte	.LVL1080
	.4byte	0x478f
	.uleb128 0x31
	.4byte	.LVL1081
	.4byte	0x4ab7
	.uleb128 0x31
	.4byte	.LVL1082
	.4byte	0x1900
	.uleb128 0x31
	.4byte	.LVL1083
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1086
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1087
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1088
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1089
	.4byte	0x180a
	.uleb128 0x31
	.4byte	.LVL1090
	.4byte	0x4a51
	.uleb128 0x31
	.4byte	.LVL1091
	.4byte	0x4ab7
	.uleb128 0x31
	.4byte	.LVL1096
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1097
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL1098
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1099
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1100
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1101
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL1102
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1103
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL1104
	.4byte	0x4954
	.uleb128 0x31
	.4byte	.LVL1105
	.4byte	0x4954
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x700
	.uleb128 0x3b
	.4byte	.LASF593
	.byte	0x4
	.2byte	0x116
	.4byte	0x64
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x47ed
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
	.4byte	.LLST85
	.uleb128 0x45
	.4byte	.LASF594
	.byte	0x4
	.2byte	0x11a
	.4byte	0x30c
	.4byte	.LLST86
	.uleb128 0x3e
	.4byte	.LASF591
	.byte	0x4
	.2byte	0x11b
	.4byte	0x4789
	.uleb128 0x31
	.4byte	.LVL375
	.4byte	0x1af8
	.byte	0
	.uleb128 0x22
	.4byte	.LASF595
	.byte	0x4
	.byte	0xac
	.4byte	0x64
	.4byte	.LFB179
	.4byte	.LFE179-.LFB179
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x489d
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x4
	.byte	0xae
	.4byte	0x30c
	.4byte	.LLST214
	.uleb128 0x4a
	.ascii	"blk\000"
	.byte	0x4
	.byte	0xaf
	.4byte	0x301
	.4byte	.LLST215
	.uleb128 0x2c
	.4byte	.LASF488
	.byte	0x4
	.byte	0xb0
	.4byte	0x322
	.4byte	.LLST216
	.uleb128 0x35
	.4byte	.LASF591
	.byte	0x4
	.byte	0xb1
	.4byte	0x4789
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x48ad
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6494
	.uleb128 0x31
	.4byte	.LVL1057
	.4byte	0x48f1
	.uleb128 0x31
	.4byte	.LVL1059
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1060
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1061
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1062
	.4byte	0x443f
	.uleb128 0x31
	.4byte	.LVL1064
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1065
	.4byte	0x5872
	.uleb128 0x31
	.4byte	.LVL1071
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1075
	.4byte	0x176f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x48ad
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xa
	.byte	0
	.uleb128 0x4
	.4byte	0x489d
	.uleb128 0x22
	.4byte	.LASF596
	.byte	0x4
	.byte	0x9c
	.4byte	0x301
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x48f1
	.uleb128 0x4a
	.ascii	"blk\000"
	.byte	0x4
	.byte	0x9e
	.4byte	0x301
	.4byte	.LLST19
	.uleb128 0x20
	.4byte	.LASF597
	.byte	0x4
	.byte	0x9f
	.4byte	0x301
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x31
	.4byte	.LVL56
	.4byte	0x4ab7
	.byte	0
	.uleb128 0x49
	.4byte	.LASF598
	.byte	0x4
	.byte	0x94
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4954
	.uleb128 0x5c
	.4byte	0x17c6
	.4byte	.LBB231
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
	.4byte	0x67e7
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x22
	.4byte	.LASF599
	.byte	0x4
	.byte	0x4f
	.4byte	0x64
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4a51
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x4
	.byte	0x51
	.4byte	0x64
	.4byte	.LLST180
	.uleb128 0x2c
	.4byte	.LASF600
	.byte	0x4
	.byte	0x52
	.4byte	0x64
	.4byte	.LLST181
	.uleb128 0x2c
	.4byte	.LASF591
	.byte	0x4
	.byte	0x53
	.4byte	0x4789
	.4byte	.LLST182
	.uleb128 0x2c
	.4byte	.LASF504
	.byte	0x4
	.byte	0x54
	.4byte	0x301
	.4byte	.LLST183
	.uleb128 0x47
	.4byte	.LASF601
	.byte	0x4
	.byte	0x67
	.uleb128 0x5d
	.4byte	.LASF362
	.4byte	0x1af3
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB443
	.4byte	.Ldebug_ranges0+0x1c8
	.byte	0x4
	.byte	0x5f
	.4byte	0x4a08
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1c8
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1c8
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL835
	.4byte	0x67e7
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL832
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL837
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL838
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL839
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL840
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL843
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL844
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL846
	.4byte	0x176f
	.byte	0
	.uleb128 0x49
	.4byte	.LASF602
	.byte	0x4
	.byte	0x41
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4aaf
	.uleb128 0x23
	.4byte	.LASF536
	.byte	0x4
	.byte	0x41
	.4byte	0x630
	.4byte	.LLST116
	.uleb128 0x23
	.4byte	.LASF603
	.byte	0x4
	.byte	0x41
	.4byte	0x539
	.4byte	.LLST117
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x43
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x29d3
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6451
	.uleb128 0x31
	.4byte	.LVL539
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL540
	.4byte	0x67f2
	.byte	0
	.uleb128 0x5e
	.4byte	.LASF604
	.byte	0x4
	.byte	0x26
	.byte	0x1
	.uleb128 0x2a
	.4byte	.LASF605
	.byte	0x4
	.byte	0x17
	.4byte	0x64
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4b12
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
	.4byte	.LASF603
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
	.4byte	.LASF606
	.byte	0x4
	.byte	0x4
	.4byte	0x64
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4b69
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
	.4byte	.LASF603
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
	.4byte	0x67f2
	.byte	0
	.uleb128 0x5f
	.4byte	.LASF668
	.byte	0x3
	.2byte	0x4bd
	.4byte	0x30c
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x3b
	.4byte	.LASF607
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x64
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4bd3
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x99
	.4byte	.LLST294
	.uleb128 0x44
	.4byte	.LASF114
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x99
	.4byte	.LLST295
	.uleb128 0x4e
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x4bd3
	.4byte	.LLST296
	.uleb128 0x24
	.4byte	.LVL1561
	.4byte	0x4cff
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x79
	.uleb128 0x3b
	.4byte	.LASF608
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x64
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c2d
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x99
	.4byte	.LLST280
	.uleb128 0x44
	.4byte	.LASF114
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x99
	.4byte	.LLST281
	.uleb128 0x4e
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x4bd3
	.4byte	.LLST282
	.uleb128 0x24
	.4byte	.LVL1460
	.4byte	0x507c
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF609
	.byte	0x3
	.2byte	0x484
	.4byte	0x64
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4cff
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x484
	.4byte	0x30c
	.4byte	.LLST245
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x484
	.4byte	0x30c
	.4byte	.LLST246
	.uleb128 0x45
	.4byte	.LASF517
	.byte	0x3
	.2byte	0x486
	.4byte	0x301
	.4byte	.LLST247
	.uleb128 0x3f
	.ascii	"tmp\000"
	.byte	0x3
	.2byte	0x487
	.4byte	0x301
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x488
	.4byte	0x30c
	.4byte	.LLST248
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x489
	.4byte	0x30c
	.4byte	.LLST249
	.uleb128 0x45
	.4byte	.LASF423
	.byte	0x3
	.2byte	0x489
	.4byte	0x30c
	.4byte	.LLST250
	.uleb128 0x3e
	.4byte	.LASF412
	.byte	0x3
	.2byte	0x48a
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL1233
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1239
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1240
	.4byte	0x3598
	.uleb128 0x31
	.4byte	.LVL1241
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1244
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1245
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1246
	.4byte	0x232e
	.byte	0
	.uleb128 0x4d
	.4byte	.LASF610
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x64
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f1f
	.uleb128 0x4e
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x2f6
	.4byte	.LLST283
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30c
	.4byte	.LLST284
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30c
	.4byte	.LLST285
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x385a
	.4byte	.LLST286
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x1d3f
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30c
	.4byte	.LLST287
	.uleb128 0x45
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30c
	.4byte	.LLST288
	.uleb128 0x45
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30c
	.4byte	.LLST289
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x30c
	.4byte	.LLST290
	.uleb128 0x3f
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x30c
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x30c
	.4byte	.LLST291
	.uleb128 0x45
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x30c
	.4byte	.LLST292
	.uleb128 0x3e
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x3c7
	.4byte	0x30c
	.uleb128 0x3e
	.4byte	.LASF615
	.byte	0x3
	.2byte	0x3c7
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF422
	.byte	0x3
	.2byte	0x3c8
	.4byte	0x12c0
	.4byte	.LLST293
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x39ff
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6387
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x2b0
	.4byte	0x4e46
	.uleb128 0x1f
	.4byte	.LASF385
	.byte	0x3
	.2byte	0x43d
	.4byte	0x32d
	.uleb128 0x2
	.byte	0x91
	.sleb128 -60
	.uleb128 0x31
	.4byte	.LVL1538
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1542
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1543
	.4byte	0x67f2
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1464
	.4byte	0x3860
	.uleb128 0x31
	.4byte	.LVL1475
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1481
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1483
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1484
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1486
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1488
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1491
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1495
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1497
	.4byte	0x3fb6
	.uleb128 0x31
	.4byte	.LVL1498
	.4byte	0x1e6d
	.uleb128 0x31
	.4byte	.LVL1499
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1500
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1505
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1510
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1514
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1517
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1518
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL1519
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1532
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1539
	.4byte	0x176f
	.uleb128 0x31
	.4byte	.LVL1544
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1552
	.4byte	0x4f43
	.uleb128 0x31
	.4byte	.LVL1554
	.4byte	0x67f2
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF616
	.byte	0x3
	.2byte	0x3b7
	.4byte	0x64
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f43
	.uleb128 0x31
	.4byte	.LVL1223
	.4byte	0x4f43
	.byte	0
	.uleb128 0x50
	.4byte	.LASF617
	.byte	0x3
	.2byte	0x387
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5067
	.uleb128 0x4e
	.ascii	"req\000"
	.byte	0x3
	.2byte	0x387
	.4byte	0xf40
	.4byte	.LLST238
	.uleb128 0x44
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x387
	.4byte	0x30c
	.4byte	.LLST239
	.uleb128 0x44
	.4byte	.LASF618
	.byte	0x3
	.2byte	0x387
	.4byte	0x30c
	.4byte	.LLST240
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x3
	.2byte	0x387
	.4byte	0x12c0
	.4byte	.LLST241
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x389
	.4byte	0x30c
	.4byte	.LLST242
	.uleb128 0x52
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x38a
	.4byte	0x30c
	.4byte	.LLST243
	.uleb128 0x45
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x38a
	.4byte	0x30c
	.4byte	.LLST244
	.uleb128 0x3e
	.4byte	.LASF405
	.byte	0x3
	.2byte	0x38b
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF380
	.byte	0x3
	.2byte	0x38c
	.4byte	0x1d3f
	.uleb128 0x3e
	.4byte	.LASF470
	.byte	0x3
	.2byte	0x38d
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x5077
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6359
	.uleb128 0x31
	.4byte	.LVL1202
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1204
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1205
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1206
	.4byte	0x1e11
	.uleb128 0x31
	.4byte	.LVL1207
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL1208
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL1210
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1212
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1214
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1217
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1218
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1221
	.4byte	0x67f2
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x5077
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xc
	.byte	0
	.uleb128 0x4
	.4byte	0x5067
	.uleb128 0x4d
	.4byte	.LASF619
	.byte	0x3
	.2byte	0x32d
	.4byte	0x64
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x525b
	.uleb128 0x4e
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x32d
	.4byte	0x2f6
	.4byte	.LLST265
	.uleb128 0x44
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x32d
	.4byte	0x30c
	.4byte	.LLST266
	.uleb128 0x44
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x32d
	.4byte	0x30c
	.4byte	.LLST267
	.uleb128 0x44
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x32d
	.4byte	0x385a
	.4byte	.LLST268
	.uleb128 0x51
	.ascii	"ret\000"
	.byte	0x3
	.2byte	0x32f
	.4byte	0x64
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x45
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x330
	.4byte	0x30c
	.4byte	.LLST269
	.uleb128 0x45
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x330
	.4byte	0x30c
	.4byte	.LLST270
	.uleb128 0x45
	.4byte	.LASF620
	.byte	0x3
	.2byte	0x330
	.4byte	0x30c
	.4byte	.LLST271
	.uleb128 0x52
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x331
	.4byte	0x30c
	.4byte	.LLST272
	.uleb128 0x52
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x331
	.4byte	0x30c
	.4byte	.LLST273
	.uleb128 0x52
	.ascii	"n\000"
	.byte	0x3
	.2byte	0x333
	.4byte	0x30c
	.4byte	.LLST274
	.uleb128 0x45
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x333
	.4byte	0x30c
	.4byte	.LLST275
	.uleb128 0x45
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x333
	.4byte	0x30c
	.4byte	.LLST276
	.uleb128 0x45
	.4byte	.LASF621
	.byte	0x3
	.2byte	0x334
	.4byte	0x30c
	.4byte	.LLST277
	.uleb128 0x3e
	.4byte	.LASF622
	.byte	0x3
	.2byte	0x334
	.4byte	0x30c
	.uleb128 0x45
	.4byte	.LASF623
	.byte	0x3
	.2byte	0x335
	.4byte	0x30c
	.4byte	.LLST278
	.uleb128 0x45
	.4byte	.LASF624
	.byte	0x3
	.2byte	0x336
	.4byte	0x30c
	.4byte	.LLST279
	.uleb128 0x53
	.4byte	.LBB537
	.4byte	.LBE537-.LBB537
	.4byte	0x51ca
	.uleb128 0x3e
	.4byte	.LASF392
	.byte	0x3
	.2byte	0x37a
	.4byte	0x301
	.uleb128 0x31
	.4byte	.LVL1432
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL1433
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB538
	.4byte	.LBE538-.LBB538
	.byte	0x3
	.2byte	0x350
	.4byte	0x5224
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB539
	.4byte	.LBE539-.LBB539
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB540
	.4byte	.LBE540-.LBB540
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL1437
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1392
	.4byte	0x378a
	.uleb128 0x31
	.4byte	.LVL1398
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1413
	.4byte	0x1c72
	.uleb128 0x31
	.4byte	.LVL1416
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1423
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1429
	.4byte	0x176f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF625
	.byte	0x3
	.2byte	0x325
	.4byte	0x64
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x527f
	.uleb128 0x31
	.4byte	.LVL1228
	.4byte	0x527f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF626
	.byte	0x3
	.2byte	0x31c
	.4byte	0x64
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52be
	.uleb128 0x31
	.4byte	.LVL1224
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1225
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1226
	.4byte	0x373d
	.uleb128 0x31
	.4byte	.LVL1227
	.4byte	0x3598
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF627
	.byte	0x3
	.2byte	0x2e6
	.4byte	0x64
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x534d
	.uleb128 0x3e
	.4byte	.LASF628
	.byte	0x3
	.2byte	0x2e8
	.4byte	0x534d
	.uleb128 0x40
	.4byte	.LASF629
	.byte	0x3
	.2byte	0x2f3
	.uleb128 0x53
	.4byte	.LBB550
	.4byte	.LBE550-.LBB550
	.4byte	0x530d
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x310
	.4byte	0x30c
	.uleb128 0x31
	.4byte	.LVL1647
	.4byte	0x1c72
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1640
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1641
	.4byte	0x5821
	.uleb128 0x31
	.4byte	.LVL1642
	.4byte	0x5699
	.uleb128 0x31
	.4byte	.LVL1643
	.4byte	0x559d
	.uleb128 0x31
	.4byte	.LVL1644
	.4byte	0x43c0
	.uleb128 0x31
	.4byte	.LVL1645
	.4byte	0x47ed
	.uleb128 0x31
	.4byte	.LVL1646
	.4byte	0x2815
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x419
	.uleb128 0x4d
	.4byte	.LASF630
	.byte	0x3
	.2byte	0x26e
	.4byte	0x64
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x544c
	.uleb128 0x3e
	.4byte	.LASF500
	.byte	0x3
	.2byte	0x270
	.4byte	0x301
	.uleb128 0x45
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x270
	.4byte	0x301
	.4byte	.LLST299
	.uleb128 0x45
	.4byte	.LASF632
	.byte	0x3
	.2byte	0x271
	.4byte	0x301
	.4byte	.LLST300
	.uleb128 0x3f
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x272
	.4byte	0x301
	.uleb128 0x48
	.4byte	.LASF633
	.byte	0x3
	.2byte	0x2bc
	.4byte	.L1784
	.uleb128 0x48
	.4byte	.LASF634
	.byte	0x3
	.2byte	0x2cc
	.4byte	.L1786
	.uleb128 0x31
	.4byte	.LVL1609
	.4byte	0x43c0
	.uleb128 0x31
	.4byte	.LVL1610
	.4byte	0x47ed
	.uleb128 0x31
	.4byte	.LVL1611
	.4byte	0x45c5
	.uleb128 0x31
	.4byte	.LVL1618
	.4byte	0x4954
	.uleb128 0x31
	.4byte	.LVL1619
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1620
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL1621
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1623
	.4byte	0x544c
	.uleb128 0x31
	.4byte	.LVL1627
	.4byte	0x5872
	.uleb128 0x31
	.4byte	.LVL1628
	.4byte	0x43c0
	.uleb128 0x31
	.4byte	.LVL1630
	.4byte	0x544c
	.uleb128 0x31
	.4byte	.LVL1632
	.4byte	0x544c
	.uleb128 0x31
	.4byte	.LVL1636
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1637
	.4byte	0x4247
	.uleb128 0x31
	.4byte	.LVL1638
	.4byte	0x3598
	.uleb128 0x31
	.4byte	.LVL1639
	.4byte	0x2815
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF635
	.byte	0x3
	.2byte	0x20f
	.4byte	0x64
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5573
	.uleb128 0x44
	.4byte	.LASF500
	.byte	0x3
	.2byte	0x20f
	.4byte	0x301
	.4byte	.LLST99
	.uleb128 0x44
	.4byte	.LASF636
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2f6
	.4byte	.LLST100
	.uleb128 0x45
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x211
	.4byte	0x301
	.4byte	.LLST101
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
	.4byte	.LASF637
	.byte	0x3
	.2byte	0x213
	.4byte	0x301
	.4byte	.LLST102
	.uleb128 0x45
	.4byte	.LASF638
	.byte	0x3
	.2byte	0x214
	.4byte	0x301
	.4byte	.LLST103
	.uleb128 0x45
	.4byte	.LASF639
	.byte	0x3
	.2byte	0x215
	.4byte	0x301
	.4byte	.LLST104
	.uleb128 0x45
	.4byte	.LASF640
	.byte	0x3
	.2byte	0x216
	.4byte	0x301
	.4byte	.LLST105
	.uleb128 0x48
	.4byte	.LASF641
	.byte	0x3
	.2byte	0x240
	.4byte	.L482
	.uleb128 0x31
	.4byte	.LVL424
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL431
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL439
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL443
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL444
	.4byte	0x5573
	.uleb128 0x31
	.4byte	.LVL445
	.4byte	0x4ab7
	.uleb128 0x31
	.4byte	.LVL450
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL455
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL456
	.4byte	0x5573
	.uleb128 0x31
	.4byte	.LVL457
	.4byte	0x4ab7
	.uleb128 0x31
	.4byte	.LVL460
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL462
	.4byte	0x436d
	.byte	0
	.uleb128 0x3c
	.4byte	.LASF642
	.byte	0x3
	.2byte	0x201
	.4byte	0x64
	.byte	0x1
	.4byte	0x559d
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x3
	.2byte	0x201
	.4byte	0x301
	.uleb128 0x3e
	.4byte	.LASF643
	.byte	0x3
	.2byte	0x203
	.4byte	0x301
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF644
	.byte	0x3
	.2byte	0x1ec
	.4byte	0x64
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5699
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB432
	.4byte	.LBE432-.LBB432
	.byte	0x3
	.2byte	0x1f9
	.4byte	0x5611
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB433
	.4byte	.LBE433-.LBB433
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB434
	.4byte	.LBE434-.LBB434
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL764
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x54
	.4byte	0x17c6
	.4byte	.LBB435
	.4byte	.LBE435-.LBB435
	.byte	0x3
	.2byte	0x1fa
	.4byte	0x566b
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB436
	.4byte	.LBE436-.LBB436
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB437
	.4byte	.LBE437-.LBB437
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL765
	.4byte	0x67fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL761
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL762
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL763
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL766
	.4byte	0x230f
	.uleb128 0x31
	.4byte	.LVL767
	.4byte	0x31cc
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF645
	.byte	0x3
	.2byte	0x116
	.4byte	0x64
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5821
	.uleb128 0x52
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x118
	.4byte	0x30c
	.4byte	.LLST115
	.uleb128 0x3e
	.4byte	.LASF646
	.byte	0x3
	.2byte	0x119
	.4byte	0x30c
	.uleb128 0x38
	.4byte	.LASF362
	.4byte	0x48ad
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6235
	.uleb128 0x31
	.4byte	.LVL496
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL497
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL498
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL499
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL500
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL501
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL502
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL503
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL504
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL505
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL506
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL507
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL508
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL509
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL510
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL511
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL512
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL513
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL514
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL515
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL516
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL517
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL518
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL519
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL520
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL521
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL522
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL523
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL524
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL525
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL526
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL527
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL528
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL529
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL530
	.4byte	0x6813
	.uleb128 0x31
	.4byte	.LVL533
	.4byte	0x67f2
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF647
	.byte	0x3
	.byte	0xb6
	.4byte	0x64
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5872
	.uleb128 0x23
	.4byte	.LASF628
	.byte	0x3
	.byte	0xb6
	.4byte	0x534d
	.4byte	.LLST11
	.uleb128 0x4a
	.ascii	"i\000"
	.byte	0x3
	.byte	0xb8
	.4byte	0x30c
	.4byte	.LLST12
	.uleb128 0x31
	.4byte	.LVL28
	.4byte	0x58b3
	.uleb128 0x31
	.4byte	.LVL29
	.4byte	0x58b3
	.uleb128 0x31
	.4byte	.LVL32
	.4byte	0x5872
	.byte	0
	.uleb128 0x22
	.4byte	.LASF648
	.byte	0x3
	.byte	0xa4
	.4byte	0x64
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x589b
	.uleb128 0x23
	.4byte	.LASF649
	.byte	0x3
	.byte	0xa4
	.4byte	0x301
	.4byte	.LLST10
	.byte	0
	.uleb128 0x58
	.4byte	.LASF650
	.byte	0x3
	.byte	0x9e
	.byte	0x1
	.4byte	0x58b3
	.uleb128 0x34
	.4byte	.LASF651
	.byte	0x3
	.byte	0x9e
	.4byte	0x30c
	.byte	0
	.uleb128 0x22
	.4byte	.LASF652
	.byte	0x3
	.byte	0x7e
	.4byte	0x301
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x58f2
	.uleb128 0x2b
	.ascii	"num\000"
	.byte	0x3
	.byte	0x7e
	.4byte	0x30c
	.4byte	.LLST9
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x3
	.byte	0x80
	.4byte	0x301
	.uleb128 0x20
	.4byte	.LASF653
	.byte	0x3
	.byte	0x81
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x52
	.byte	0
	.uleb128 0x22
	.4byte	.LASF654
	.byte	0x3
	.byte	0x70
	.4byte	0x64
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5938
	.uleb128 0x23
	.4byte	.LASF85
	.byte	0x3
	.byte	0x70
	.4byte	0xb6
	.4byte	.LLST65
	.uleb128 0x29
	.ascii	"buf\000"
	.byte	0x3
	.byte	0x72
	.4byte	0xb6
	.uleb128 0x31
	.4byte	.LVL286
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL287
	.4byte	0x5938
	.byte	0
	.uleb128 0x22
	.4byte	.LASF655
	.byte	0x3
	.byte	0xa
	.4byte	0x64
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c2e
	.uleb128 0x23
	.4byte	.LASF85
	.byte	0x3
	.byte	0xa
	.4byte	0xb6
	.4byte	.LLST62
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
	.4byte	.LBB289
	.4byte	.LBE289-.LBB289
	.4byte	0x59a3
	.uleb128 0x2c
	.4byte	.LASF392
	.byte	0x3
	.byte	0x56
	.4byte	0x301
	.4byte	.LLST63
	.uleb128 0x31
	.4byte	.LVL277
	.4byte	0x3fb6
	.uleb128 0x31
	.4byte	.LVL279
	.4byte	0x681e
	.byte	0
	.uleb128 0x53
	.4byte	.LBB290
	.4byte	.LBE290-.LBB290
	.4byte	0x59c9
	.uleb128 0x2c
	.4byte	.LASF392
	.byte	0x3
	.byte	0x5f
	.4byte	0x301
	.4byte	.LLST64
	.uleb128 0x31
	.4byte	.LVL283
	.4byte	0x681e
	.byte	0
	.uleb128 0x31
	.4byte	.LVL207
	.4byte	0x682a
	.uleb128 0x31
	.4byte	.LVL208
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL209
	.4byte	0x682a
	.uleb128 0x31
	.4byte	.LVL210
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL211
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL212
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL213
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL214
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL215
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL216
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL217
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL218
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL219
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL220
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL221
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL222
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL223
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL224
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL225
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL226
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL227
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL228
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL229
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL230
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL231
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL232
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL233
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL234
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL235
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL236
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL237
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL238
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL239
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL240
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL241
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL242
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL243
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL244
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL245
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL246
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL247
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL248
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL249
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL250
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL251
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL252
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL253
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL254
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL255
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL256
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL257
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL258
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL259
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL260
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL261
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL262
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL263
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL264
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL265
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL266
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL267
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL268
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL269
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL270
	.4byte	0x278b
	.uleb128 0x31
	.4byte	.LVL271
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL272
	.4byte	0x273c
	.uleb128 0x31
	.4byte	.LVL273
	.4byte	0x681e
	.uleb128 0x31
	.4byte	.LVL276
	.4byte	0x681e
	.byte	0
	.uleb128 0x60
	.4byte	0x3419
	.4byte	.LFB286
	.4byte	.LFE286-.LFB286
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c50
	.uleb128 0x61
	.4byte	0x3426
	.4byte	.LLST8
	.uleb128 0x2e
	.4byte	0x3432
	.byte	0
	.uleb128 0x60
	.4byte	0x589b
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c6b
	.uleb128 0x62
	.4byte	0x58a7
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x60
	.4byte	0x5573
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5ca3
	.uleb128 0x61
	.4byte	0x5584
	.4byte	.LLST13
	.uleb128 0x30
	.4byte	0x5590
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x2e
	.4byte	0x5584
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x30
	.4byte	0x5c87
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x63
	.4byte	0x4aaf
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x63
	.4byte	0x43b4
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x63
	.4byte	0x43a8
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x60
	.4byte	0x4188
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5d6a
	.uleb128 0x61
	.4byte	0x4198
	.4byte	.LLST28
	.uleb128 0x30
	.4byte	0x41a3
	.uleb128 0x30
	.4byte	0x41ae
	.uleb128 0x30
	.4byte	0x41b9
	.uleb128 0x30
	.4byte	0x41c4
	.uleb128 0x30
	.4byte	0x41cf
	.uleb128 0x30
	.4byte	0x41da
	.uleb128 0x30
	.4byte	0x41e5
	.uleb128 0x30
	.4byte	0x41f0
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x68
	.uleb128 0x2e
	.4byte	0x4198
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x68
	.uleb128 0x64
	.4byte	0x5cec
	.4byte	.LLST29
	.uleb128 0x64
	.4byte	0x5cf1
	.4byte	.LLST30
	.uleb128 0x64
	.4byte	0x5cf6
	.4byte	.LLST31
	.uleb128 0x64
	.4byte	0x5cfb
	.4byte	.LLST32
	.uleb128 0x64
	.4byte	0x5d00
	.4byte	.LLST33
	.uleb128 0x64
	.4byte	0x5d05
	.4byte	.LLST34
	.uleb128 0x30
	.4byte	0x5d0a
	.uleb128 0x64
	.4byte	0x5d0f
	.4byte	.LLST35
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x41fd
	.4byte	.LFB195
	.4byte	.LFE195-.LFB195
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5db8
	.uleb128 0x61
	.4byte	0x4209
	.4byte	.LLST36
	.uleb128 0x30
	.4byte	0x4214
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x80
	.4byte	0x5dae
	.uleb128 0x2e
	.4byte	0x4209
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x80
	.uleb128 0x30
	.4byte	0x5d86
	.uleb128 0x24
	.4byte	.LVL118
	.4byte	0x67f2
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL117
	.4byte	0x4188
	.byte	0
	.uleb128 0x60
	.4byte	0x4222
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5e06
	.uleb128 0x61
	.4byte	0x422e
	.4byte	.LLST39
	.uleb128 0x30
	.4byte	0x4239
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x98
	.4byte	0x5dfc
	.uleb128 0x2e
	.4byte	0x422e
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x98
	.uleb128 0x30
	.4byte	0x5dd4
	.uleb128 0x24
	.4byte	.LVL128
	.4byte	0x67f2
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL127
	.4byte	0x4119
	.byte	0
	.uleb128 0x60
	.4byte	0x3f37
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5eba
	.uleb128 0x61
	.4byte	0x3f48
	.4byte	.LLST48
	.uleb128 0x30
	.4byte	0x3f54
	.uleb128 0x30
	.4byte	0x3f60
	.uleb128 0x30
	.4byte	0x3f6c
	.uleb128 0x30
	.4byte	0x3f78
	.uleb128 0x30
	.4byte	0x3f84
	.uleb128 0x30
	.4byte	0x3f90
	.uleb128 0x30
	.4byte	0x3f9c
	.uleb128 0x30
	.4byte	0x3fa8
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xb0
	.uleb128 0x2e
	.4byte	0x3f48
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xb0
	.uleb128 0x30
	.4byte	0x5e22
	.uleb128 0x64
	.4byte	0x5e27
	.4byte	.LLST49
	.uleb128 0x64
	.4byte	0x5e2c
	.4byte	.LLST50
	.uleb128 0x64
	.4byte	0x5e31
	.4byte	.LLST51
	.uleb128 0x64
	.4byte	0x5e36
	.4byte	.LLST52
	.uleb128 0x65
	.4byte	0x5e3b
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x30
	.4byte	0x5e40
	.uleb128 0x30
	.4byte	0x5e45
	.uleb128 0x31
	.4byte	.LVL162
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL166
	.4byte	0x4097
	.uleb128 0x31
	.4byte	.LVL167
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL168
	.4byte	0x41fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x3719
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5ef2
	.uleb128 0x61
	.4byte	0x3726
	.4byte	.LLST53
	.uleb128 0x30
	.4byte	0x3732
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xc8
	.uleb128 0x2e
	.4byte	0x3726
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xc8
	.uleb128 0x30
	.4byte	0x5ed6
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x3419
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f21
	.uleb128 0x61
	.4byte	0x3426
	.4byte	.LLST54
	.uleb128 0x61
	.4byte	0x3432
	.4byte	.LLST55
	.uleb128 0x24
	.4byte	.LVL178
	.4byte	0x5c2e
	.byte	0
	.uleb128 0x60
	.4byte	0x2d11
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f61
	.uleb128 0x62
	.4byte	0x2d1e
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x2d2a
	.uleb128 0x33
	.4byte	.LBB287
	.4byte	.LBE287-.LBB287
	.uleb128 0x2e
	.4byte	0x2d1e
	.uleb128 0x33
	.4byte	.LBB288
	.4byte	.LBE288-.LBB288
	.uleb128 0x65
	.4byte	0x5f3b
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
	.4byte	0x5f96
	.uleb128 0x61
	.4byte	0x2685
	.4byte	.LLST71
	.uleb128 0x33
	.4byte	.LBB293
	.4byte	.LBE293-.LBB293
	.uleb128 0x2e
	.4byte	0x2685
	.uleb128 0x31
	.4byte	.LVL314
	.4byte	0x4222
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x1e04
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fbc
	.uleb128 0x31
	.4byte	.LVL351
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL352
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x60
	.4byte	0x436d
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6035
	.uleb128 0x61
	.4byte	0x4379
	.4byte	.LLST97
	.uleb128 0x61
	.4byte	0x4384
	.4byte	.LLST98
	.uleb128 0x66
	.4byte	0x43a8
	.4byte	.LBB310
	.4byte	.LBE310-.LBB310
	.byte	0x2
	.byte	0x65
	.uleb128 0x33
	.4byte	.LBB312
	.4byte	.LBE312-.LBB312
	.uleb128 0x2e
	.4byte	0x4384
	.uleb128 0x2e
	.4byte	0x4379
	.uleb128 0x33
	.4byte	.LBB313
	.4byte	.LBE313-.LBB313
	.uleb128 0x30
	.4byte	0x4390
	.uleb128 0x33
	.4byte	.LBB314
	.4byte	.LBE314-.LBB314
	.uleb128 0x30
	.4byte	0x439a
	.uleb128 0x31
	.4byte	.LVL417
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL418
	.4byte	0x1954
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x3d87
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x60ac
	.uleb128 0x61
	.4byte	0x3d98
	.4byte	.LLST106
	.uleb128 0x64
	.4byte	0x3da4
	.4byte	.LLST107
	.uleb128 0x30
	.4byte	0x3db0
	.uleb128 0x30
	.4byte	0x3dbc
	.uleb128 0x53
	.4byte	.LBB323
	.4byte	.LBE323-.LBB323
	.4byte	0x6099
	.uleb128 0x2e
	.4byte	0x3d98
	.uleb128 0x33
	.4byte	.LBB324
	.4byte	.LBE324-.LBB324
	.uleb128 0x30
	.4byte	0x6051
	.uleb128 0x30
	.4byte	0x605a
	.uleb128 0x30
	.4byte	0x605f
	.uleb128 0x31
	.4byte	.LVL472
	.4byte	0x67f2
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL469
	.4byte	0x4247
	.uleb128 0x31
	.4byte	.LVL470
	.4byte	0x67f2
	.byte	0
	.uleb128 0x60
	.4byte	0x1900
	.4byte	.LFB275
	.4byte	.LFE275-.LFB275
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x61c1
	.uleb128 0x61
	.4byte	0x1910
	.4byte	.LLST108
	.uleb128 0x30
	.4byte	0x191b
	.uleb128 0x30
	.4byte	0x1926
	.uleb128 0x30
	.4byte	0x1931
	.uleb128 0x33
	.4byte	.LBB337
	.4byte	.LBE337-.LBB337
	.uleb128 0x2e
	.4byte	0x1910
	.uleb128 0x33
	.4byte	.LBB338
	.4byte	.LBE338-.LBB338
	.uleb128 0x30
	.4byte	0x60c8
	.uleb128 0x65
	.4byte	0x60cd
	.uleb128 0x3
	.byte	0x91
	.sleb128 -80
	.uleb128 0x65
	.4byte	0x60d2
	.uleb128 0x3
	.byte	0x91
	.sleb128 -100
	.uleb128 0x67
	.4byte	0x193c
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB339
	.4byte	.Ldebug_ranges0+0x130
	.byte	0x1
	.byte	0xa7
	.4byte	0x615b
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x130
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x130
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x31
	.4byte	.LVL476
	.4byte	0x67e7
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB345
	.4byte	.Ldebug_ranges0+0x148
	.byte	0x1
	.byte	0xa8
	.4byte	0x61a3
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x148
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x148
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL477
	.4byte	0x1954
	.uleb128 0x31
	.4byte	.LVL478
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL479
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
	.4byte	0x623e
	.uleb128 0x61
	.4byte	0x17d6
	.4byte	.LLST112
	.uleb128 0x61
	.4byte	0x17df
	.4byte	.LLST113
	.uleb128 0x61
	.4byte	0x17e8
	.4byte	.LLST114
	.uleb128 0x53
	.4byte	.LBB377
	.4byte	.LBE377-.LBB377
	.4byte	0x6207
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.byte	0
	.uleb128 0x33
	.4byte	.LBB378
	.4byte	.LBE378-.LBB378
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x33
	.4byte	.LBB379
	.4byte	.LBE379-.LBB379
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x31
	.4byte	.LVL495
	.4byte	0x67e7
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x1d5a
	.4byte	.LFB268
	.4byte	.LFE268-.LFB268
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x62dc
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
	.4byte	.Ldebug_ranges0+0x1b0
	.4byte	0x62c8
	.uleb128 0x30
	.4byte	0x6251
	.uleb128 0x30
	.4byte	0x6258
	.uleb128 0x64
	.4byte	0x625d
	.4byte	.LLST147
	.uleb128 0x65
	.4byte	0x6262
	.uleb128 0x3
	.byte	0x91
	.sleb128 -80
	.uleb128 0x53
	.4byte	.LBB400
	.4byte	.LBE400-.LBB400
	.4byte	0x62be
	.uleb128 0x65
	.4byte	0x1d9e
	.uleb128 0x3
	.byte	0x91
	.sleb128 -100
	.uleb128 0x31
	.4byte	.LVL659
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL660
	.4byte	0x451a
	.uleb128 0x31
	.4byte	.LVL661
	.4byte	0x1e6d
	.byte	0
	.uleb128 0x31
	.4byte	.LVL658
	.4byte	0x3939
	.byte	0
	.uleb128 0x33
	.4byte	.LBB403
	.4byte	.LBE403-.LBB403
	.uleb128 0x64
	.4byte	0x1dac
	.4byte	.LLST148
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x1dba
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6367
	.uleb128 0x61
	.4byte	0x1dcb
	.4byte	.LLST189
	.uleb128 0x30
	.4byte	0x1dd7
	.uleb128 0x30
	.4byte	0x1de3
	.uleb128 0x30
	.4byte	0x1ded
	.uleb128 0x30
	.4byte	0x1df7
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x200
	.uleb128 0x2e
	.4byte	0x1dcb
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x200
	.uleb128 0x30
	.4byte	0x62f8
	.uleb128 0x65
	.4byte	0x62fd
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x64
	.4byte	0x6302
	.4byte	.LLST190
	.uleb128 0x65
	.4byte	0x6307
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x31
	.4byte	.LVL884
	.4byte	0x1e04
	.uleb128 0x31
	.4byte	.LVL886
	.4byte	0x4582
	.uleb128 0x31
	.4byte	.LVL887
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL888
	.4byte	0x4b12
	.uleb128 0x31
	.4byte	.LVL889
	.4byte	0x4954
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x23a4
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x63e1
	.uleb128 0x61
	.4byte	0x23b5
	.4byte	.LLST191
	.uleb128 0x30
	.4byte	0x23c1
	.uleb128 0x53
	.4byte	.LBB464
	.4byte	.LBE464-.LBB464
	.4byte	0x63d7
	.uleb128 0x2e
	.4byte	0x23b5
	.uleb128 0x33
	.4byte	.LBB465
	.4byte	.LBE465-.LBB465
	.uleb128 0x30
	.4byte	0x6383
	.uleb128 0x31
	.4byte	.LVL894
	.4byte	0x4097
	.uleb128 0x31
	.4byte	.LVL895
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL896
	.4byte	0x2674
	.uleb128 0x31
	.4byte	.LVL897
	.4byte	0x1dba
	.uleb128 0x31
	.4byte	.LVL898
	.4byte	0x67f2
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL901
	.4byte	0x3f37
	.byte	0
	.uleb128 0x60
	.4byte	0x365f
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6459
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x218
	.uleb128 0x64
	.4byte	0x366d
	.4byte	.LLST193
	.uleb128 0x64
	.4byte	0x3679
	.4byte	.LLST194
	.uleb128 0x64
	.4byte	0x3685
	.4byte	.LLST195
	.uleb128 0x65
	.4byte	0x3691
	.uleb128 0x2
	.byte	0x91
	.sleb128 -52
	.uleb128 0x30
	.4byte	0x369d
	.uleb128 0x31
	.4byte	.LVL909
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL910
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL913
	.4byte	0x17c6
	.uleb128 0x31
	.4byte	.LVL915
	.4byte	0x23e4
	.uleb128 0x31
	.4byte	.LVL916
	.4byte	0x19ea
	.uleb128 0x31
	.4byte	.LVL917
	.4byte	0x232e
	.byte	0
	.byte	0
	.uleb128 0x60
	.4byte	0x2b83
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6476
	.uleb128 0x24
	.4byte	.LVL1125
	.4byte	0x3860
	.byte	0
	.uleb128 0x60
	.4byte	0x343f
	.4byte	.LFB221
	.4byte	.LFE221-.LFB221
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x64a5
	.uleb128 0x31
	.4byte	.LVL1153
	.4byte	0x378a
	.uleb128 0x31
	.4byte	.LVL1154
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1155
	.4byte	0x17c6
	.byte	0
	.uleb128 0x60
	.4byte	0x1f6f
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x64d4
	.uleb128 0x31
	.4byte	.LVL1280
	.4byte	0x1f78
	.uleb128 0x31
	.4byte	.LVL1281
	.4byte	0x36ab
	.uleb128 0x31
	.4byte	.LVL1282
	.4byte	0x20c4
	.byte	0
	.uleb128 0x60
	.4byte	0x1c72
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x66c5
	.uleb128 0x61
	.4byte	0x1c83
	.4byte	.LLST256
	.uleb128 0x61
	.4byte	0x1c8f
	.4byte	.LLST257
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
	.4byte	.Ldebug_ranges0+0x268
	.4byte	0x66bb
	.uleb128 0x2e
	.4byte	0x1c8f
	.uleb128 0x2e
	.4byte	0x1c83
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x268
	.uleb128 0x64
	.4byte	0x64f9
	.4byte	.LLST258
	.uleb128 0x64
	.4byte	0x64fe
	.4byte	.LLST259
	.uleb128 0x64
	.4byte	0x6503
	.4byte	.LLST260
	.uleb128 0x30
	.4byte	0x6508
	.uleb128 0x30
	.4byte	0x650d
	.uleb128 0x30
	.4byte	0x6512
	.uleb128 0x64
	.4byte	0x6517
	.4byte	.LLST261
	.uleb128 0x64
	.4byte	0x651c
	.4byte	.LLST262
	.uleb128 0x64
	.4byte	0x6521
	.4byte	.LLST263
	.uleb128 0x64
	.4byte	0x6526
	.4byte	.LLST264
	.uleb128 0x67
	.4byte	0x1d13
	.uleb128 0x67
	.4byte	0x1d1b
	.uleb128 0x30
	.4byte	0x652b
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x290
	.4byte	0x65e1
	.uleb128 0x30
	.4byte	0x1d31
	.uleb128 0x31
	.4byte	.LVL1307
	.4byte	0x2692
	.uleb128 0x31
	.4byte	.LVL1308
	.4byte	0x3fb6
	.uleb128 0x31
	.4byte	.LVL1309
	.4byte	0x3fb6
	.uleb128 0x31
	.4byte	.LVL1310
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1312
	.4byte	0x1e04
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1324
	.4byte	0x2255
	.uleb128 0x31
	.4byte	.LVL1325
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1330
	.4byte	0x3fb6
	.uleb128 0x31
	.4byte	.LVL1333
	.4byte	0x1d5a
	.uleb128 0x31
	.4byte	.LVL1334
	.4byte	0x3fb6
	.uleb128 0x31
	.4byte	.LVL1335
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1339
	.4byte	0x2255
	.uleb128 0x31
	.4byte	.LVL1340
	.4byte	0x232e
	.uleb128 0x31
	.4byte	.LVL1341
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1342
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1343
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1344
	.4byte	0x2c55
	.uleb128 0x31
	.4byte	.LVL1346
	.4byte	0x1d5a
	.uleb128 0x31
	.4byte	.LVL1361
	.4byte	0x1af8
	.uleb128 0x31
	.4byte	.LVL1363
	.4byte	0x67f2
	.uleb128 0x31
	.4byte	.LVL1365
	.4byte	0x3939
	.uleb128 0x31
	.4byte	.LVL1366
	.4byte	0x1f15
	.uleb128 0x31
	.4byte	.LVL1369
	.4byte	0x2287
	.uleb128 0x31
	.4byte	.LVL1370
	.4byte	0x1ea1
	.uleb128 0x31
	.4byte	.LVL1377
	.4byte	0x1ea1
	.uleb128 0x31
	.4byte	.LVL1378
	.4byte	0x23a4
	.uleb128 0x31
	.4byte	.LVL1379
	.4byte	0x4f1f
	.uleb128 0x31
	.4byte	.LVL1380
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1381
	.4byte	0x3598
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1311
	.4byte	0x20c4
	.byte	0
	.uleb128 0x60
	.4byte	0x2815
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x67d3
	.uleb128 0x30
	.4byte	0x2826
	.uleb128 0x53
	.4byte	.LBB547
	.4byte	.LBE547-.LBB547
	.4byte	0x676f
	.uleb128 0x64
	.4byte	0x66d8
	.4byte	.LLST298
	.uleb128 0x39
	.4byte	.Ldebug_ranges0+0x2c8
	.4byte	0x6702
	.uleb128 0x30
	.4byte	0x2831
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1595
	.4byte	0x2a94
	.uleb128 0x31
	.4byte	.LVL1596
	.4byte	0x2a94
	.uleb128 0x31
	.4byte	.LVL1597
	.4byte	0x36ab
	.uleb128 0x31
	.4byte	.LVL1598
	.4byte	0x36ab
	.uleb128 0x31
	.4byte	.LVL1599
	.4byte	0x3a04
	.uleb128 0x31
	.4byte	.LVL1600
	.4byte	0x3598
	.uleb128 0x31
	.4byte	.LVL1601
	.4byte	0x3598
	.uleb128 0x31
	.4byte	.LVL1604
	.4byte	0x1e6d
	.uleb128 0x31
	.4byte	.LVL1605
	.4byte	0x1e6d
	.uleb128 0x31
	.4byte	.LVL1606
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1607
	.4byte	0x2460
	.uleb128 0x31
	.4byte	.LVL1608
	.4byte	0x29d8
	.byte	0
	.uleb128 0x31
	.4byte	.LVL1583
	.4byte	0x43c0
	.uleb128 0x31
	.4byte	.LVL1584
	.4byte	0x344c
	.uleb128 0x31
	.4byte	.LVL1585
	.4byte	0x333f
	.uleb128 0x31
	.4byte	.LVL1586
	.4byte	0x319f
	.uleb128 0x31
	.4byte	.LVL1587
	.4byte	0x3201
	.uleb128 0x31
	.4byte	.LVL1588
	.4byte	0x2b09
	.uleb128 0x31
	.4byte	.LVL1589
	.4byte	0x343f
	.uleb128 0x31
	.4byte	.LVL1590
	.4byte	0x4329
	.uleb128 0x31
	.4byte	.LVL1591
	.4byte	0x2b8c
	.uleb128 0x31
	.4byte	.LVL1592
	.4byte	0x2cc0
	.uleb128 0x31
	.4byte	.LVL1593
	.4byte	0x3719
	.byte	0
	.uleb128 0x68
	.4byte	.LASF656
	.4byte	.LASF656
	.byte	0x11
	.byte	0x6f
	.uleb128 0x69
	.4byte	.LASF669
	.4byte	.LASF669
	.uleb128 0x68
	.4byte	.LASF657
	.4byte	.LASF657
	.byte	0x12
	.byte	0x19
	.uleb128 0x68
	.4byte	.LASF658
	.4byte	.LASF658
	.byte	0xb
	.byte	0x8e
	.uleb128 0x68
	.4byte	.LASF659
	.4byte	.LASF659
	.byte	0x12
	.byte	0x1b
	.uleb128 0x68
	.4byte	.LASF660
	.4byte	.LASF660
	.byte	0x13
	.byte	0x27
	.uleb128 0x68
	.4byte	.LASF661
	.4byte	.LASF661
	.byte	0xd
	.byte	0x18
	.uleb128 0x6a
	.4byte	.LASF662
	.4byte	.LASF662
	.byte	0xc
	.2byte	0x19b
	.uleb128 0x6b
	.4byte	.LASF670
	.4byte	.LASF671
	.byte	0x14
	.byte	0
	.4byte	.LASF670
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
	.uleb128 0x67
	.uleb128 0xa
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
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
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
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
	.uleb128 0x5
	.byte	0
	.byte	0
	.uleb128 0x6b
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
.LLST301:
	.4byte	.LVL1648-.Ltext0
	.4byte	.LVL1649-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST302:
	.4byte	.LVL1648-.Ltext0
	.4byte	.LVL1649-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST303:
	.4byte	.LVL1648-.Ltext0
	.4byte	.LVL1649-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST177:
	.4byte	.LVL829-.Ltext0
	.4byte	.LVL830-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST178:
	.4byte	.LVL829-.Ltext0
	.4byte	.LVL830-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST179:
	.4byte	.LVL829-.Ltext0
	.4byte	.LVL830-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST109:
	.4byte	.LVL483-.Ltext0
	.4byte	.LVL485-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL485-1-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST110:
	.4byte	.LVL483-.Ltext0
	.4byte	.LVL484-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL484-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST111:
	.4byte	.LVL487-.Ltext0
	.4byte	.LVL488-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL490-.Ltext0
	.4byte	.LVL492-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL493-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST94:
	.4byte	.LVL405-.Ltext0
	.4byte	.LVL406-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL407-.Ltext0
	.4byte	.LVL410-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL411-.Ltext0
	.4byte	.LVL412-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST95:
	.4byte	.LVL405-.Ltext0
	.4byte	.LVL406-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST96:
	.4byte	.LVL405-.Ltext0
	.4byte	.LVL406-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL406-.Ltext0
	.4byte	.LVL411-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL411-.Ltext0
	.4byte	.LFE274-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST89:
	.4byte	.LVL384-.Ltext0
	.4byte	.LVL386-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST90:
	.4byte	.LVL384-.Ltext0
	.4byte	.LVL390-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL390-.Ltext0
	.4byte	.LVL398-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL402-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST91:
	.4byte	.LVL384-.Ltext0
	.4byte	.LVL390-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL390-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST92:
	.4byte	.LVL384-.Ltext0
	.4byte	.LVL385-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL389-.Ltext0
	.4byte	.LVL390-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL395-.Ltext0
	.4byte	.LVL396-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL402-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST93:
	.4byte	.LVL387-.Ltext0
	.4byte	.LVL395-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL403-.Ltext0
	.4byte	.LVL404-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST82:
	.4byte	.LVL353-.Ltext0
	.4byte	.LVL357-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL358-.Ltext0
	.4byte	.LVL360-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL361-.Ltext0
	.4byte	.LVL363-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST83:
	.4byte	.LVL353-.Ltext0
	.4byte	.LVL358-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL358-.Ltext0
	.4byte	.LFE272-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST84:
	.4byte	.LVL353-.Ltext0
	.4byte	.LVL359-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL361-.Ltext0
	.4byte	.LVL362-.Ltext0
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
.LLST81:
	.4byte	.LVL346-.Ltext0
	.4byte	.LVL347-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL347-1-.Ltext0
	.4byte	.LVL350-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST80:
	.4byte	.LVL342-.Ltext0
	.4byte	.LVL343-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL343-.Ltext0
	.4byte	.LFE264-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST255:
	.4byte	.LVL1295-.Ltext0
	.4byte	.LVL1296-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST168:
	.4byte	.LVL787-.Ltext0
	.4byte	.LVL791-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL791-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST169:
	.4byte	.LVL787-.Ltext0
	.4byte	.LVL789-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL789-.Ltext0
	.4byte	.LVL790-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL791-.Ltext0
	.4byte	.LVL792-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL792-.Ltext0
	.4byte	.LVL795-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL799-.Ltext0
	.4byte	.LVL802-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL816-.Ltext0
	.4byte	.LVL817-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL826-.Ltext0
	.4byte	.LVL827-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST170:
	.4byte	.LVL796-.Ltext0
	.4byte	.LVL797-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL805-.Ltext0
	.4byte	.LVL806-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL809-.Ltext0
	.4byte	.LVL810-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST171:
	.4byte	.LVL794-.Ltext0
	.4byte	.LVL807-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL808-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST172:
	.4byte	.LVL803-.Ltext0
	.4byte	.LVL804-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL816-.Ltext0
	.4byte	.LVL819-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST173:
	.4byte	.LVL788-.Ltext0
	.4byte	.LVL800-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL801-.Ltext0
	.4byte	.LVL823-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL826-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST174:
	.4byte	.LVL811-.Ltext0
	.4byte	.LVL812-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL813-.Ltext0
	.4byte	.LVL818-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL821-.Ltext0
	.4byte	.LVL822-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST175:
	.4byte	.LVL793-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST176:
	.4byte	.LVL814-.Ltext0
	.4byte	.LVL815-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL821-.Ltext0
	.4byte	.LVL822-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST251:
	.4byte	.LVL1249-.Ltext0
	.4byte	.LVL1250-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1253-.Ltext0
	.4byte	.LVL1254-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1255-.Ltext0
	.4byte	.LVL1256-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST252:
	.4byte	.LVL1252-.Ltext0
	.4byte	.LVL1253-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1258-.Ltext0
	.4byte	.LVL1262-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1263-.Ltext0
	.4byte	.LFE258-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST253:
	.4byte	.LVL1251-.Ltext0
	.4byte	.LVL1257-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1262-.Ltext0
	.4byte	.LVL1263-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1264-.Ltext0
	.4byte	.LVL1265-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST254:
	.4byte	.LVL1267-.Ltext0
	.4byte	.LVL1270-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1273-.Ltext0
	.4byte	.LFE258-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST75:
	.4byte	.LVL330-.Ltext0
	.4byte	.LVL331-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL331-.Ltext0
	.4byte	.LVL341-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST76:
	.4byte	.LVL330-.Ltext0
	.4byte	.LVL332-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL332-1-.Ltext0
	.4byte	.LVL341-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST77:
	.4byte	.LVL330-.Ltext0
	.4byte	.LVL332-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL332-1-.Ltext0
	.4byte	.LVL341-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST78:
	.4byte	.LVL333-.Ltext0
	.4byte	.LVL336-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL337-.Ltext0
	.4byte	.LVL340-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST79:
	.4byte	.LVL334-.Ltext0
	.4byte	.LVL335-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL337-.Ltext0
	.4byte	.LVL338-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL339-.Ltext0
	.4byte	.LFE257-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST74:
	.4byte	.LVL326-.Ltext0
	.4byte	.LVL327-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL328-.Ltext0
	.4byte	.LVL329-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST192:
	.4byte	.LVL902-.Ltext0
	.4byte	.LVL904-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL904-.Ltext0
	.4byte	.LVL906-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL906-.Ltext0
	.4byte	.LFE252-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST72:
	.4byte	.LVL316-.Ltext0
	.4byte	.LVL317-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL317-.Ltext0
	.4byte	.LFE250-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST73:
	.4byte	.LVL320-.Ltext0
	.4byte	.LVL321-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL323-.Ltext0
	.4byte	.LFE250-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST237:
	.4byte	.LVL1187-.Ltext0
	.4byte	.LVL1189-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1189-.Ltext0
	.4byte	.LFE249-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST184:
	.4byte	.LVL849-.Ltext0
	.4byte	.LVL851-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL851-.Ltext0
	.4byte	.LVL881-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST185:
	.4byte	.LVL850-.Ltext0
	.4byte	.LVL881-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST186:
	.4byte	.LVL850-.Ltext0
	.4byte	.LVL873-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL874-.Ltext0
	.4byte	.LVL881-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST187:
	.4byte	.LVL868-.Ltext0
	.4byte	.LVL869-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL870-.Ltext0
	.4byte	.LVL871-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL876-.Ltext0
	.4byte	.LVL877-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL878-.Ltext0
	.4byte	.LVL879-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST188:
	.4byte	.LVL852-.Ltext0
	.4byte	.LVL853-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL854-.Ltext0
	.4byte	.LVL855-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL860-.Ltext0
	.4byte	.LVL861-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST66:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL293-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL297-.Ltext0
	.4byte	.LVL305-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL308-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST67:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL293-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL296-.Ltext0
	.4byte	.LVL299-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL308-.Ltext0
	.4byte	.LVL311-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL312-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST68:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL293-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL295-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST69:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL293-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL300-.Ltext0
	.4byte	.LVL303-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL304-.Ltext0
	.4byte	.LVL308-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST70:
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL298-.Ltext0
	.4byte	.LVL301-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL302-.Ltext0
	.4byte	.LVL306-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL308-.Ltext0
	.4byte	.LVL309-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL310-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST61:
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
.LLST60:
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
.LLST149:
	.4byte	.LVL665-.Ltext0
	.4byte	.LVL668-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL673-.Ltext0
	.4byte	.LVL676-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST150:
	.4byte	.LVL669-.Ltext0
	.4byte	.LVL673-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL676-.Ltext0
	.4byte	.LFE240-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST151:
	.4byte	.LVL670-.Ltext0
	.4byte	.LVL673-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST235:
	.4byte	.LVL1177-.Ltext0
	.4byte	.LVL1179-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1182-.Ltext0
	.4byte	.LFE239-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST236:
	.4byte	.LVL1174-.Ltext0
	.4byte	.LVL1178-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1179-.Ltext0
	.4byte	.LVL1182-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST213:
	.4byte	.LVL1051-.Ltext0
	.4byte	.LVL1052-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1052-.Ltext0
	.4byte	.LFE238-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST165:
	.4byte	.LVL771-.Ltext0
	.4byte	.LVL783-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST166:
	.4byte	.LVL769-.Ltext0
	.4byte	.LVL778-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL779-.Ltext0
	.4byte	.LVL781-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST167:
	.4byte	.LVL768-.Ltext0
	.4byte	.LVL775-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL776-.Ltext0
	.4byte	.LVL783-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST59:
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
.LLST197:
	.4byte	.LVL923-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL930-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST198:
	.4byte	.LVL924-.Ltext0
	.4byte	.LVL925-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL926-.Ltext0
	.4byte	.LVL931-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL942-.Ltext0
	.4byte	.LVL944-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL948-.Ltext0
	.4byte	.LVL949-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL951-.Ltext0
	.4byte	.LVL952-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL952-.Ltext0
	.4byte	.LVL953-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL966-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL966-.Ltext0
	.4byte	.LVL967-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL973-.Ltext0
	.4byte	.LVL974-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL989-.Ltext0
	.4byte	.LVL990-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL991-.Ltext0
	.4byte	.LVL992-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL998-.Ltext0
	.4byte	.LVL999-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST199:
	.4byte	.LVL925-.Ltext0
	.4byte	.LVL927-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL944-.Ltext0
	.4byte	.LVL945-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL953-.Ltext0
	.4byte	.LVL954-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL954-.Ltext0
	.4byte	.LVL955-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL968-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL969-.Ltext0
	.4byte	.LVL970-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL974-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL990-.Ltext0
	.4byte	.LVL992-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL998-.Ltext0
	.4byte	.LVL999-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST200:
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL946-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL977-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL982-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL984-.Ltext0
	.4byte	.LVL987-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL990-.Ltext0
	.4byte	.LVL997-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL998-.Ltext0
	.4byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1005-.Ltext0
	.4byte	.LVL1007-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1008-.Ltext0
	.4byte	.LVL1010-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1041-.Ltext0
	.4byte	.LVL1042-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1048-.Ltext0
	.4byte	.LVL1050-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST201:
	.4byte	.LVL943-.Ltext0
	.4byte	.LVL947-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL963-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL993-.Ltext0
	.4byte	.LVL995-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1048-.Ltext0
	.4byte	.LVL1049-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST202:
	.4byte	.LVL938-.Ltext0
	.4byte	.LVL939-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL940-.Ltext0
	.4byte	.LVL941-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL941-.Ltext0
	.4byte	.LVL943-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL960-.Ltext0
	.4byte	.LVL961-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL963-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL992-.Ltext0
	.4byte	.LVL993-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL994-.Ltext0
	.4byte	.LVL995-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST203:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL932-.Ltext0
	.4byte	.LVL933-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL933-.Ltext0
	.4byte	.LVL948-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL950-.Ltext0
	.4byte	.LVL964-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL973-.Ltext0
	.4byte	.LVL976-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST204:
	.4byte	.LVL937-.Ltext0
	.4byte	.LVL939-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL956-.Ltext0
	.4byte	.LVL957-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL959-.Ltext0
	.4byte	.LVL960-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST205:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL976-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL984-.Ltext0
	.4byte	.LVL986-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL996-.Ltext0
	.4byte	.LVL1000-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1009-.Ltext0
	.4byte	.LVL1018-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1020-.Ltext0
	.4byte	.LVL1039-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1050-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST206:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL948-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL1050-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1050-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST207:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL948-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL975-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	0
	.4byte	0
.LLST208:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL945-.Ltext0
	.4byte	.LVL948-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL1026-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1026-.Ltext0
	.4byte	.LVL1027-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1027-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST209:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL976-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL983-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL984-.Ltext0
	.4byte	.LVL986-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL997-.Ltext0
	.4byte	.LVL1000-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1018-.Ltext0
	.4byte	.LVL1020-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1039-.Ltext0
	.4byte	.LVL1041-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST210:
	.4byte	.LVL935-.Ltext0
	.4byte	.LVL936-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL965-.Ltext0
	.4byte	.LVL966-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL981-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL984-.Ltext0
	.4byte	.LVL988-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL998-.Ltext0
	.4byte	.LVL1000-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1001-.Ltext0
	.4byte	.LVL1004-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1005-.Ltext0
	.4byte	.LVL1006-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1044-.Ltext0
	.4byte	.LVL1045-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1046-.Ltext0
	.4byte	.LVL1047-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1047-.Ltext0
	.4byte	.LVL1048-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST211:
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL975-.Ltext0
	.4byte	.LVL985-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL997-.Ltext0
	.4byte	.LVL1000-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1014-.Ltext0
	.4byte	.LVL1020-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1022-.Ltext0
	.4byte	.LVL1033-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1039-.Ltext0
	.4byte	.LVL1041-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST212:
	.4byte	.LVL1014-.Ltext0
	.4byte	.LVL1015-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1024-.Ltext0
	.4byte	.LVL1025-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1029-.Ltext0
	.4byte	.LVL1032-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST142:
	.4byte	.LVL632-.Ltext0
	.4byte	.LVL633-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL633-.Ltext0
	.4byte	.LVL635-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL635-.Ltext0
	.4byte	.LVL649-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST143:
	.4byte	.LVL632-.Ltext0
	.4byte	.LVL633-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL634-.Ltext0
	.4byte	.LVL636-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL636-1-.Ltext0
	.4byte	.LFE230-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -28
	.4byte	0
	.4byte	0
.LLST144:
	.4byte	.LVL637-.Ltext0
	.4byte	.LVL649-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST145:
	.4byte	.LVL639-.Ltext0
	.4byte	.LVL645-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL646-.Ltext0
	.4byte	.LVL649-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST146:
	.4byte	.LVL638-.Ltext0
	.4byte	.LVL640-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL644-.Ltext0
	.4byte	.LVL647-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST57:
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
.LLST58:
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
.LLST158:
	.4byte	.LVL714-.Ltext0
	.4byte	.LVL720-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL720-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST159:
	.4byte	.LVL725-.Ltext0
	.4byte	.LVL729-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL735-.Ltext0
	.4byte	.LVL736-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL743-.Ltext0
	.4byte	.LVL746-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST160:
	.4byte	.LVL731-.Ltext0
	.4byte	.LVL732-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST161:
	.4byte	.LVL715-.Ltext0
	.4byte	.LVL717-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL723-.Ltext0
	.4byte	.LVL726-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL726-.Ltext0
	.4byte	.LVL727-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL734-.Ltext0
	.4byte	.LVL736-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL736-1-.Ltext0
	.4byte	.LVL740-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL740-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL742-.Ltext0
	.4byte	.LVL743-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL743-.Ltext0
	.4byte	.LVL745-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL748-.Ltext0
	.4byte	.LVL749-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL751-.Ltext0
	.4byte	.LVL752-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL753-.Ltext0
	.4byte	.LVL754-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL755-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST162:
	.4byte	.LVL718-.Ltext0
	.4byte	.LVL731-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL735-.Ltext0
	.4byte	.LVL740-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL743-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST163:
	.4byte	.LVL717-.Ltext0
	.4byte	.LVL719-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL730-.Ltext0
	.4byte	.LVL733-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST164:
	.4byte	.LVL719-.Ltext0
	.4byte	.LVL721-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL722-.Ltext0
	.4byte	.LVL723-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL724-.Ltext0
	.4byte	.LVL728-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL735-.Ltext0
	.4byte	.LVL736-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL743-.Ltext0
	.4byte	.LVL744-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST297:
	.4byte	.LVL1563-.Ltext0
	.4byte	.LVL1564-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1566-.Ltext0
	.4byte	.LVL1572-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1579-.Ltext0
	.4byte	.LVL1581-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1582-.Ltext0
	.4byte	.LFE224-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST56:
	.4byte	.LVL181-.Ltext0
	.4byte	.LVL182-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST155:
	.4byte	.LVL692-.Ltext0
	.4byte	.LVL700-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL701-.Ltext0
	.4byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST156:
	.4byte	.LVL693-.Ltext0
	.4byte	.LVL697-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL698-.Ltext0
	.4byte	.LFE220-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST157:
	.4byte	.LVL693-.Ltext0
	.4byte	.LVL694-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL699-.Ltext0
	.4byte	.LVL702-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL703-.Ltext0
	.4byte	.LVL711-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL712-.Ltext0
	.4byte	.LVL713-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST233:
	.4byte	.LVL1159-.Ltext0
	.4byte	.LVL1173-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST234:
	.4byte	.LVL1164-.Ltext0
	.4byte	.LVL1169-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1170-.Ltext0
	.4byte	.LVL1173-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST152:
	.4byte	.LVL678-.Ltext0
	.4byte	.LVL683-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL684-.Ltext0
	.4byte	.LVL687-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL687-.Ltext0
	.4byte	.LFE217-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST153:
	.4byte	.LVL680-.Ltext0
	.4byte	.LVL681-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL684-.Ltext0
	.4byte	.LVL685-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST154:
	.4byte	.LVL679-.Ltext0
	.4byte	.LVL680-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST225:
	.4byte	.LVL1126-.Ltext0
	.4byte	.LVL1130-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1132-.Ltext0
	.4byte	.LVL1134-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST226:
	.4byte	.LVL1127-.Ltext0
	.4byte	.LVL1128-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1129-.Ltext0
	.4byte	.LVL1131-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1132-.Ltext0
	.4byte	.LVL1133-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST227:
	.4byte	.LVL1137-.Ltext0
	.4byte	.LVL1141-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1141-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST228:
	.4byte	.LVL1137-.Ltext0
	.4byte	.LVL1141-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1141-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST229:
	.4byte	.LVL1137-.Ltext0
	.4byte	.LVL1138-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1138-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST230:
	.4byte	.LVL1140-.Ltext0
	.4byte	.LVL1142-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1146-.Ltext0
	.4byte	.LVL1152-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST231:
	.4byte	.LVL1143-.Ltext0
	.4byte	.LVL1144-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1145-.Ltext0
	.4byte	.LVL1147-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1150-.Ltext0
	.4byte	.LVL1151-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST232:
	.4byte	.LVL1139-.Ltext0
	.4byte	.LVL1140-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST219:
	.4byte	.LVL1107-.Ltext0
	.4byte	.LVL1111-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1111-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST220:
	.4byte	.LVL1107-.Ltext0
	.4byte	.LVL1111-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1111-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST221:
	.4byte	.LVL1107-.Ltext0
	.4byte	.LVL1108-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1108-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST222:
	.4byte	.LVL1110-.Ltext0
	.4byte	.LVL1115-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1116-.Ltext0
	.4byte	.LVL1124-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST223:
	.4byte	.LVL1112-.Ltext0
	.4byte	.LVL1113-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1114-.Ltext0
	.4byte	.LVL1117-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1121-.Ltext0
	.4byte	.LVL1122-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST224:
	.4byte	.LVL1109-.Ltext0
	.4byte	.LVL1124-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST138:
	.4byte	.LVL619-.Ltext0
	.4byte	.LVL622-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL623-.Ltext0
	.4byte	.LVL625-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL628-.Ltext0
	.4byte	.LVL629-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST139:
	.4byte	.LVL619-.Ltext0
	.4byte	.LVL623-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL623-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST140:
	.4byte	.LVL619-.Ltext0
	.4byte	.LVL623-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL623-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST141:
	.4byte	.LVL624-.Ltext0
	.4byte	.LVL630-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL631-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST196:
	.4byte	.LVL919-.Ltext0
	.4byte	.LVL920-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL921-.Ltext0
	.4byte	.LVL922-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL922-.Ltext0
	.4byte	.LFE211-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST137:
	.4byte	.LVL616-.Ltext0
	.4byte	.LVL617-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST130:
	.4byte	.LVL587-.Ltext0
	.4byte	.LVL589-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL589-.Ltext0
	.4byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST131:
	.4byte	.LVL587-.Ltext0
	.4byte	.LVL589-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL589-.Ltext0
	.4byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST132:
	.4byte	.LVL587-.Ltext0
	.4byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST133:
	.4byte	.LVL593-.Ltext0
	.4byte	.LVL594-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST134:
	.4byte	.LVL588-.Ltext0
	.4byte	.LVL596-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL597-.Ltext0
	.4byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST126:
	.4byte	.LVL570-.Ltext0
	.4byte	.LVL572-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL572-.Ltext0
	.4byte	.LVL575-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL575-.Ltext0
	.4byte	.LVL578-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL578-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST127:
	.4byte	.LVL580-.Ltext0
	.4byte	.LVL581-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL584-.Ltext0
	.4byte	.LVL585-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL586-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST128:
	.4byte	.LVL571-.Ltext0
	.4byte	.LVL576-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST129:
	.4byte	.LVL574-.Ltext0
	.4byte	.LVL575-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL577-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST135:
	.4byte	.LVL601-.Ltext0
	.4byte	.LVL602-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL602-.Ltext0
	.4byte	.LVL605-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL605-1-.Ltext0
	.4byte	.LFE206-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST136:
	.4byte	.LVL603-.Ltext0
	.4byte	.LVL608-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL610-.Ltext0
	.4byte	.LVL612-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST121:
	.4byte	.LVL554-.Ltext0
	.4byte	.LVL555-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL555-.Ltext0
	.4byte	.LVL559-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL559-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST122:
	.4byte	.LVL558-.Ltext0
	.4byte	.LVL564-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL566-.Ltext0
	.4byte	.LVL568-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST123:
	.4byte	.LVL561-.Ltext0
	.4byte	.LVL562-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL567-.Ltext0
	.4byte	.LVL568-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST124:
	.4byte	.LVL563-.Ltext0
	.4byte	.LVL566-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL568-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST125:
	.4byte	.LVL565-.Ltext0
	.4byte	.LVL566-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL568-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST118:
	.4byte	.LVL542-.Ltext0
	.4byte	.LVL543-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL543-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST119:
	.4byte	.LVL542-.Ltext0
	.4byte	.LVL543-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL543-.Ltext0
	.4byte	.LVL549-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST120:
	.4byte	.LVL546-.Ltext0
	.4byte	.LVL547-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL549-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST46:
	.4byte	.LVL146-.Ltext0
	.4byte	.LVL150-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL151-.Ltext0
	.4byte	.LVL152-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL154-.Ltext0
	.4byte	.LVL155-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST47:
	.4byte	.LVL147-.Ltext0
	.4byte	.LVL148-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL149-.Ltext0
	.4byte	.LVL151-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL153-.Ltext0
	.4byte	.LVL156-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST45:
	.4byte	.LVL144-.Ltext0
	.4byte	.LVL145-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST42:
	.4byte	.LVL135-.Ltext0
	.4byte	.LVL139-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL140-.Ltext0
	.4byte	.LVL143-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST43:
	.4byte	.LVL135-.Ltext0
	.4byte	.LVL138-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL140-.Ltext0
	.4byte	.LVL141-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL142-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST44:
	.4byte	.LVL136-.Ltext0
	.4byte	.LVL137-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL140-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST40:
	.4byte	.LVL129-.Ltext0
	.4byte	.LVL132-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL132-.Ltext0
	.4byte	.LFE198-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST41:
	.4byte	.LVL129-.Ltext0
	.4byte	.LVL131-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL132-.Ltext0
	.4byte	.LVL133-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST37:
	.4byte	.LVL119-.Ltext0
	.4byte	.LVL123-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL124-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST38:
	.4byte	.LVL122-.Ltext0
	.4byte	.LVL124-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL125-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST27:
	.4byte	.LVL81-.Ltext0
	.4byte	.LVL83-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL84-.Ltext0
	.4byte	.LVL85-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST26:
	.4byte	.LVL79-.Ltext0
	.4byte	.LVL80-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST25:
	.4byte	.LVL74-.Ltext0
	.4byte	.LVL75-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST87:
	.4byte	.LVL376-.Ltext0
	.4byte	.LVL378-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST88:
	.4byte	.LVL376-.Ltext0
	.4byte	.LVL377-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL377-.Ltext0
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
.LLST217:
	.4byte	.LVL1078-.Ltext0
	.4byte	.LVL1106-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST218:
	.4byte	.LVL1080-.Ltext0
	.4byte	.LVL1084-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1085-.Ltext0
	.4byte	.LVL1094-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1095-.Ltext0
	.4byte	.LVL1106-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST85:
	.4byte	.LVL368-.Ltext0
	.4byte	.LVL370-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL371-.Ltext0
	.4byte	.LVL372-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL373-.Ltext0
	.4byte	.LFE180-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST86:
	.4byte	.LVL374-.Ltext0
	.4byte	.LVL375-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST214:
	.4byte	.LVL1066-.Ltext0
	.4byte	.LVL1067-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1074-.Ltext0
	.4byte	.LVL1076-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST215:
	.4byte	.LVL1058-.Ltext0
	.4byte	.LVL1063-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1067-.Ltext0
	.4byte	.LVL1068-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1069-.Ltext0
	.4byte	.LVL1070-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1076-.Ltext0
	.4byte	.LVL1077-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST216:
	.4byte	.LVL1063-.Ltext0
	.4byte	.LVL1066-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1070-.Ltext0
	.4byte	.LVL1072-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1073-.Ltext0
	.4byte	.LVL1074-.Ltext0
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
.LLST180:
	.4byte	.LVL831-.Ltext0
	.4byte	.LVL834-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL845-.Ltext0
	.4byte	.LVL847-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST181:
	.4byte	.LVL836-.Ltext0
	.4byte	.LVL845-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL847-.Ltext0
	.4byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST182:
	.4byte	.LVL833-.Ltext0
	.4byte	.LVL845-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL847-.Ltext0
	.4byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST183:
	.4byte	.LVL834-.Ltext0
	.4byte	.LVL841-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL842-.Ltext0
	.4byte	.LVL845-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL847-.Ltext0
	.4byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST116:
	.4byte	.LVL536-.Ltext0
	.4byte	.LVL538-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST117:
	.4byte	.LVL536-.Ltext0
	.4byte	.LVL537-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL537-.Ltext0
	.4byte	.LVL541-.Ltext0
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
.LLST294:
	.4byte	.LVL1557-.Ltext0
	.4byte	.LVL1560-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST295:
	.4byte	.LVL1557-.Ltext0
	.4byte	.LVL1559-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST296:
	.4byte	.LVL1557-.Ltext0
	.4byte	.LVL1558-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST280:
	.4byte	.LVL1456-.Ltext0
	.4byte	.LVL1459-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST281:
	.4byte	.LVL1456-.Ltext0
	.4byte	.LVL1458-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST282:
	.4byte	.LVL1456-.Ltext0
	.4byte	.LVL1457-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST245:
	.4byte	.LVL1229-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1230-.Ltext0
	.4byte	.LVL1232-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1232-.Ltext0
	.4byte	.LVL1236-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1248-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST246:
	.4byte	.LVL1229-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1230-.Ltext0
	.4byte	.LVL1233-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1233-1-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST247:
	.4byte	.LVL1231-.Ltext0
	.4byte	.LVL1232-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1237-.Ltext0
	.4byte	.LVL1248-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST248:
	.4byte	.LVL1231-.Ltext0
	.4byte	.LVL1232-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1235-.Ltext0
	.4byte	.LVL1248-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST249:
	.4byte	.LVL1231-.Ltext0
	.4byte	.LVL1232-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	.LVL1238-.Ltext0
	.4byte	.LVL1242-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	.LVL1242-.Ltext0
	.4byte	.LVL1243-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1244-.Ltext0
	.4byte	.LVL1246-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	.LVL1246-.Ltext0
	.4byte	.LVL1247-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1247-.Ltext0
	.4byte	.LVL1248-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -32
	.4byte	0
	.4byte	0
.LLST250:
	.4byte	.LVL1231-.Ltext0
	.4byte	.LVL1232-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -28
	.4byte	.LVL1238-.Ltext0
	.4byte	.LVL1248-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -28
	.4byte	0
	.4byte	0
.LLST283:
	.4byte	.LVL1461-.Ltext0
	.4byte	.LVL1463-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1465-.Ltext0
	.4byte	.LVL1468-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1555-.Ltext0
	.4byte	.LVL1556-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST284:
	.4byte	.LVL1461-.Ltext0
	.4byte	.LVL1464-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1555-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST285:
	.4byte	.LVL1461-.Ltext0
	.4byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1465-.Ltext0
	.4byte	.LVL1466-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1466-.Ltext0
	.4byte	.LVL1467-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1503-.Ltext0
	.4byte	.LVL1504-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1508-.Ltext0
	.4byte	.LVL1509-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1521-.Ltext0
	.4byte	.LVL1522-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1524-.Ltext0
	.4byte	.LVL1531-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1532-.Ltext0
	.4byte	.LVL1533-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1536-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST286:
	.4byte	.LVL1461-.Ltext0
	.4byte	.LVL1464-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1465-.Ltext0
	.4byte	.LVL1466-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1509-.Ltext0
	.4byte	.LVL1510-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1525-.Ltext0
	.4byte	.LVL1526-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1526-.Ltext0
	.4byte	.LVL1527-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1528-.Ltext0
	.4byte	.LVL1530-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1545-.Ltext0
	.4byte	.LVL1546-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1549-.Ltext0
	.4byte	.LVL1550-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST287:
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1465-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1478-.Ltext0
	.4byte	.LVL1501-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1513-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST288:
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1465-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1470-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST289:
	.4byte	.LVL1471-.Ltext0
	.4byte	.LVL1474-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1494-.Ltext0
	.4byte	.LVL1495-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1520-.Ltext0
	.4byte	.LVL1521-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1529-.Ltext0
	.4byte	.LVL1530-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1540-.Ltext0
	.4byte	.LVL1541-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1547-.Ltext0
	.4byte	.LVL1548-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST290:
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1465-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1478-.Ltext0
	.4byte	.LVL1501-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1513-.Ltext0
	.4byte	.LVL1534-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	.LVL1534-.Ltext0
	.4byte	.LVL1535-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1535-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -64
	.4byte	0
	.4byte	0
.LLST291:
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1465-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1478-.Ltext0
	.4byte	.LVL1482-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1492-.Ltext0
	.4byte	.LVL1501-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1515-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST292:
	.4byte	.LVL1472-.Ltext0
	.4byte	.LVL1473-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1473-.Ltext0
	.4byte	.LVL1475-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1475-.Ltext0
	.4byte	.LVL1476-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1477-.Ltext0
	.4byte	.LVL1479-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1479-.Ltext0
	.4byte	.LVL1480-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1489-.Ltext0
	.4byte	.LVL1490-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1491-.Ltext0
	.4byte	.LVL1493-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1493-.Ltext0
	.4byte	.LVL1494-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1501-.Ltext0
	.4byte	.LVL1502-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1511-.Ltext0
	.4byte	.LVL1513-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1515-.Ltext0
	.4byte	.LVL1516-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1550-.Ltext0
	.4byte	.LVL1551-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1553-.Ltext0
	.4byte	.LVL1554-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1554-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST293:
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1465-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1477-.Ltext0
	.4byte	.LVL1485-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1487-.Ltext0
	.4byte	.LVL1496-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1512-.Ltext0
	.4byte	.LVL1555-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST238:
	.4byte	.LVL1198-.Ltext0
	.4byte	.LVL1202-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1202-1-.Ltext0
	.4byte	.LVL1203-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1208-.Ltext0
	.4byte	.LVL1209-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1219-.Ltext0
	.4byte	.LVL1222-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST239:
	.4byte	.LVL1198-.Ltext0
	.4byte	.LVL1202-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1202-1-.Ltext0
	.4byte	.LVL1222-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST240:
	.4byte	.LVL1198-.Ltext0
	.4byte	.LVL1200-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST241:
	.4byte	.LVL1198-.Ltext0
	.4byte	.LVL1201-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1201-.Ltext0
	.4byte	.LVL1222-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST242:
	.4byte	.LVL1199-.Ltext0
	.4byte	.LVL1222-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST243:
	.4byte	.LVL1202-.Ltext0
	.4byte	.LVL1203-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	.LVL1203-.Ltext0
	.4byte	.LVL1211-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1211-.Ltext0
	.4byte	.LFE165-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	0
	.4byte	0
.LLST244:
	.4byte	.LVL1202-.Ltext0
	.4byte	.LVL1203-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1208-.Ltext0
	.4byte	.LVL1209-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1213-.Ltext0
	.4byte	.LVL1214-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1215-.Ltext0
	.4byte	.LVL1216-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1218-.Ltext0
	.4byte	.LVL1220-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST265:
	.4byte	.LVL1389-.Ltext0
	.4byte	.LVL1391-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1394-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1395-.Ltext0
	.4byte	.LVL1398-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1455-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST266:
	.4byte	.LVL1389-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1455-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST267:
	.4byte	.LVL1389-.Ltext0
	.4byte	.LVL1390-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1395-.Ltext0
	.4byte	.LVL1396-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1396-.Ltext0
	.4byte	.LVL1397-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1444-.Ltext0
	.4byte	.LVL1445-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST268:
	.4byte	.LVL1389-.Ltext0
	.4byte	.LVL1392-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1392-1-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST269:
	.4byte	.LVL1403-.Ltext0
	.4byte	.LVL1405-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1426-.Ltext0
	.4byte	.LVL1427-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1430-.Ltext0
	.4byte	.LVL1431-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1440-.Ltext0
	.4byte	.LVL1441-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1451-.Ltext0
	.4byte	.LVL1452-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST270:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1394-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1406-.Ltext0
	.4byte	.LVL1412-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1414-.Ltext0
	.4byte	.LVL1415-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1428-.Ltext0
	.4byte	.LVL1429-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1430-.Ltext0
	.4byte	.LVL1431-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1447-.Ltext0
	.4byte	.LVL1448-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1449-.Ltext0
	.4byte	.LVL1450-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1452-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1454-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST271:
	.4byte	.LVL1417-.Ltext0
	.4byte	.LVL1418-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1435-.Ltext0
	.4byte	.LVL1436-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1437-.Ltext0
	.4byte	.LVL1438-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST272:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1404-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST273:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1409-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST274:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1409-.Ltext0
	.4byte	.LVL1417-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1422-.Ltext0
	.4byte	.LVL1434-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1438-.Ltext0
	.4byte	.LVL1439-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1451-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST275:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1400-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST276:
	.4byte	.LVL1407-.Ltext0
	.4byte	.LVL1408-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1408-.Ltext0
	.4byte	.LVL1410-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1410-.Ltext0
	.4byte	.LVL1411-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1414-.Ltext0
	.4byte	.LVL1416-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1419-.Ltext0
	.4byte	.LVL1420-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1421-.Ltext0
	.4byte	.LVL1423-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST277:
	.4byte	.LVL1424-.Ltext0
	.4byte	.LVL1425-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1442-.Ltext0
	.4byte	.LVL1443-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST278:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1399-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST279:
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1409-.Ltext0
	.4byte	.LVL1422-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL1434-.Ltext0
	.4byte	.LVL1439-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST299:
	.4byte	.LVL1614-.Ltext0
	.4byte	.LVL1617-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1631-.Ltext0
	.4byte	.LVL1633-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1634-.Ltext0
	.4byte	.LVL1635-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST300:
	.4byte	.LVL1612-.Ltext0
	.4byte	.LVL1613-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1622-.Ltext0
	.4byte	.LVL1624-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1625-.Ltext0
	.4byte	.LVL1629-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST99:
	.4byte	.LVL421-.Ltext0
	.4byte	.LVL423-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL438-.Ltext0
	.4byte	.LVL439-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL439-.Ltext0
	.4byte	.LVL440-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL440-.Ltext0
	.4byte	.LVL441-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL441-.Ltext0
	.4byte	.LVL442-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST100:
	.4byte	.LVL421-.Ltext0
	.4byte	.LVL441-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL441-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST101:
	.4byte	.LVL422-.Ltext0
	.4byte	.LVL446-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL447-.Ltext0
	.4byte	.LVL448-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL449-.Ltext0
	.4byte	.LVL458-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL459-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST102:
	.4byte	.LVL425-.Ltext0
	.4byte	.LVL427-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL428-.Ltext0
	.4byte	.LVL430-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL434-.Ltext0
	.4byte	.LVL437-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL453-.Ltext0
	.4byte	.LVL454-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST103:
	.4byte	.LVL429-.Ltext0
	.4byte	.LVL433-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL435-.Ltext0
	.4byte	.LVL436-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL439-.Ltext0
	.4byte	.LVL441-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL454-.Ltext0
	.4byte	.LVL461-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST104:
	.4byte	.LVL427-.Ltext0
	.4byte	.LVL430-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL432-.Ltext0
	.4byte	.LVL434-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL451-.Ltext0
	.4byte	.LVL453-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST105:
	.4byte	.LVL426-.Ltext0
	.4byte	.LVL441-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL452-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST115:
	.4byte	.LVL531-.Ltext0
	.4byte	.LVL532-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL534-.Ltext0
	.4byte	.LVL535-.Ltext0
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
.LLST65:
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
.LLST62:
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
.LLST63:
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
.LLST64:
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
.LLST28:
	.4byte	.LVL86-.Ltext0
	.4byte	.LVL87-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL88-.Ltext0
	.4byte	.LVL92-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL93-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST29:
	.4byte	.LVL89-.Ltext0
	.4byte	.LVL104-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL105-.Ltext0
	.4byte	.LVL106-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL107-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST30:
	.4byte	.LVL90-.Ltext0
	.4byte	.LVL112-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL113-.Ltext0
	.4byte	.LVL114-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL115-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST31:
	.4byte	.LVL94-.Ltext0
	.4byte	.LVL96-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL107-.Ltext0
	.4byte	.LVL109-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST32:
	.4byte	.LVL91-.Ltext0
	.4byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL97-.Ltext0
	.4byte	.LVL107-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	.LVL109-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x5b
	.4byte	0
	.4byte	0
.LLST33:
	.4byte	.LVL91-.Ltext0
	.4byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL95-.Ltext0
	.4byte	.LVL107-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL108-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST34:
	.4byte	.LVL91-.Ltext0
	.4byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL100-.Ltext0
	.4byte	.LVL101-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL102-.Ltext0
	.4byte	.LVL107-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL110-.Ltext0
	.4byte	.LVL111-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST35:
	.4byte	.LVL91-.Ltext0
	.4byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL97-.Ltext0
	.4byte	.LVL98-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL99-.Ltext0
	.4byte	.LVL103-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL109-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST36:
	.4byte	.LVL116-.Ltext0
	.4byte	.LVL117-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST39:
	.4byte	.LVL126-.Ltext0
	.4byte	.LVL127-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST48:
	.4byte	.LVL157-.Ltext0
	.4byte	.LVL162-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL162-.Ltext0
	.4byte	.LVL170-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL170-.Ltext0
	.4byte	.LVL172-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL172-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST49:
	.4byte	.LVL159-.Ltext0
	.4byte	.LVL161-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL170-.Ltext0
	.4byte	.LVL172-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST50:
	.4byte	.LVL160-.Ltext0
	.4byte	.LVL170-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL171-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST51:
	.4byte	.LVL163-.Ltext0
	.4byte	.LVL165-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL168-.Ltext0
	.4byte	.LVL169-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL172-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST52:
	.4byte	.LVL164-.Ltext0
	.4byte	.LVL166-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL168-.Ltext0
	.4byte	.LVL170-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL173-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST53:
	.4byte	.LVL175-.Ltext0
	.4byte	.LVL176-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST54:
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
.LLST55:
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
.LLST71:
	.4byte	.LVL313-.Ltext0
	.4byte	.LVL314-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL314-.Ltext0
	.4byte	.LVL315-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST97:
	.4byte	.LVL416-.Ltext0
	.4byte	.LVL419-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL419-.Ltext0
	.4byte	.LVL420-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL420-.Ltext0
	.4byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST98:
	.4byte	.LVL416-.Ltext0
	.4byte	.LVL417-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL419-.Ltext0
	.4byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST106:
	.4byte	.LVL467-.Ltext0
	.4byte	.LVL471-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL471-.Ltext0
	.4byte	.LVL473-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST107:
	.4byte	.LVL468-.Ltext0
	.4byte	.LVL473-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST108:
	.4byte	.LVL474-.Ltext0
	.4byte	.LVL475-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL479-.Ltext0
	.4byte	.LVL480-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL481-.Ltext0
	.4byte	.LVL482-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL482-.Ltext0
	.4byte	.LFE275-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST112:
	.4byte	.LVL494-.Ltext0
	.4byte	.LVL495-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL495-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST113:
	.4byte	.LVL494-.Ltext0
	.4byte	.LVL495-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL495-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST114:
	.4byte	.LVL494-.Ltext0
	.4byte	.LVL495-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL495-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST147:
	.4byte	.LVL657-.Ltext0
	.4byte	.LVL662-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -104
	.4byte	0
	.4byte	0
.LLST148:
	.4byte	.LVL663-.Ltext0
	.4byte	.LVL664-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST189:
	.4byte	.LVL882-.Ltext0
	.4byte	.LVL884-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL884-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST190:
	.4byte	.LVL883-.Ltext0
	.4byte	.LVL890-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL891-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST191:
	.4byte	.LVL892-.Ltext0
	.4byte	.LVL893-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL893-.Ltext0
	.4byte	.LVL899-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL899-.Ltext0
	.4byte	.LVL901-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL901-1-.Ltext0
	.4byte	.LFE251-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST193:
	.4byte	.LVL908-.Ltext0
	.4byte	.LVL918-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST194:
	.4byte	.LVL909-.Ltext0
	.4byte	.LVL918-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	0
	.4byte	0
.LLST195:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL912-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL914-.Ltext0
	.4byte	.LVL918-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST256:
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1305-.Ltext0
	.4byte	.LVL1323-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1326-.Ltext0
	.4byte	.LVL1327-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1331-.Ltext0
	.4byte	.LVL1332-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1336-.Ltext0
	.4byte	.LVL1338-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1384-.Ltext0
	.4byte	.LFE269-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST257:
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1306-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1314-.Ltext0
	.4byte	.LVL1315-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1384-.Ltext0
	.4byte	.LVL1385-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST258:
	.4byte	.LVL1313-.Ltext0
	.4byte	.LVL1315-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1316-.Ltext0
	.4byte	.LVL1317-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1318-.Ltext0
	.4byte	.LVL1319-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1320-.Ltext0
	.4byte	.LVL1321-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1322-.Ltext0
	.4byte	.LVL1331-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1336-.Ltext0
	.4byte	.LVL1350-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1351-.Ltext0
	.4byte	.LVL1372-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1382-.Ltext0
	.4byte	.LVL1383-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1386-.Ltext0
	.4byte	.LVL1387-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST259:
	.4byte	.LVL1362-.Ltext0
	.4byte	.LVL1363-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1364-.Ltext0
	.4byte	.LVL1365-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST260:
	.4byte	.LVL1314-.Ltext0
	.4byte	.LVL1315-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1327-.Ltext0
	.4byte	.LVL1331-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1337-.Ltext0
	.4byte	.LVL1367-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1367-.Ltext0
	.4byte	.LVL1368-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1368-.Ltext0
	.4byte	.LVL1384-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST261:
	.4byte	.LVL1314-.Ltext0
	.4byte	.LVL1315-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1327-.Ltext0
	.4byte	.LVL1329-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1345-.Ltext0
	.4byte	.LVL1355-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1357-.Ltext0
	.4byte	.LVL1384-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST262:
	.4byte	.LVL1354-.Ltext0
	.4byte	.LVL1356-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1358-.Ltext0
	.4byte	.LVL1360-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST263:
	.4byte	.LVL1347-.Ltext0
	.4byte	.LVL1348-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1349-.Ltext0
	.4byte	.LVL1353-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1359-.Ltext0
	.4byte	.LVL1360-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1373-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1373-.Ltext0
	.4byte	.LVL1374-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1375-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST264:
	.4byte	.LVL1314-.Ltext0
	.4byte	.LVL1315-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1327-.Ltext0
	.4byte	.LVL1328-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1345-.Ltext0
	.4byte	.LVL1352-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1361-.Ltext0
	.4byte	.LVL1384-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST298:
	.4byte	.LVL1594-.Ltext0
	.4byte	.LVL1595-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1602-.Ltext0
	.4byte	.LVL1603-.Ltext0
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
	.4byte	.LBB222-.Ltext0
	.4byte	.LBE222-.Ltext0
	.4byte	.LBB225-.Ltext0
	.4byte	.LBE225-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB231-.Ltext0
	.4byte	.LBE231-.Ltext0
	.4byte	.LBB236-.Ltext0
	.4byte	.LBE236-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB242-.Ltext0
	.4byte	.LBE242-.Ltext0
	.4byte	.LBB249-.Ltext0
	.4byte	.LBE249-.Ltext0
	.4byte	.LBB250-.Ltext0
	.4byte	.LBE250-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB251-.Ltext0
	.4byte	.LBE251-.Ltext0
	.4byte	.LBB254-.Ltext0
	.4byte	.LBE254-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB257-.Ltext0
	.4byte	.LBE257-.Ltext0
	.4byte	.LBB260-.Ltext0
	.4byte	.LBE260-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB263-.Ltext0
	.4byte	.LBE263-.Ltext0
	.4byte	.LBB266-.Ltext0
	.4byte	.LBE266-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB269-.Ltext0
	.4byte	.LBE269-.Ltext0
	.4byte	.LBB272-.Ltext0
	.4byte	.LBE272-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB275-.Ltext0
	.4byte	.LBE275-.Ltext0
	.4byte	.LBB278-.Ltext0
	.4byte	.LBE278-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB281-.Ltext0
	.4byte	.LBE281-.Ltext0
	.4byte	.LBB284-.Ltext0
	.4byte	.LBE284-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB299-.Ltext0
	.4byte	.LBE299-.Ltext0
	.4byte	.LBB300-.Ltext0
	.4byte	.LBE300-.Ltext0
	.4byte	.LBB301-.Ltext0
	.4byte	.LBE301-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB315-.Ltext0
	.4byte	.LBE315-.Ltext0
	.4byte	.LBB318-.Ltext0
	.4byte	.LBE318-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB319-.Ltext0
	.4byte	.LBE319-.Ltext0
	.4byte	.LBB320-.Ltext0
	.4byte	.LBE320-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB339-.Ltext0
	.4byte	.LBE339-.Ltext0
	.4byte	.LBB344-.Ltext0
	.4byte	.LBE344-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB345-.Ltext0
	.4byte	.LBE345-.Ltext0
	.4byte	.LBB350-.Ltext0
	.4byte	.LBE350-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB361-.Ltext0
	.4byte	.LBE361-.Ltext0
	.4byte	.LBB368-.Ltext0
	.4byte	.LBE368-.Ltext0
	.4byte	.LBB369-.Ltext0
	.4byte	.LBE369-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB385-.Ltext0
	.4byte	.LBE385-.Ltext0
	.4byte	.LBB390-.Ltext0
	.4byte	.LBE390-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB391-.Ltext0
	.4byte	.LBE391-.Ltext0
	.4byte	.LBB392-.Ltext0
	.4byte	.LBE392-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB399-.Ltext0
	.4byte	.LBE399-.Ltext0
	.4byte	.LBB401-.Ltext0
	.4byte	.LBE401-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB443-.Ltext0
	.4byte	.LBE443-.Ltext0
	.4byte	.LBB450-.Ltext0
	.4byte	.LBE450-.Ltext0
	.4byte	.LBB451-.Ltext0
	.4byte	.LBE451-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB452-.Ltext0
	.4byte	.LBE452-.Ltext0
	.4byte	.LBB455-.Ltext0
	.4byte	.LBE455-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB458-.Ltext0
	.4byte	.LBE458-.Ltext0
	.4byte	.LBB461-.Ltext0
	.4byte	.LBE461-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB470-.Ltext0
	.4byte	.LBE470-.Ltext0
	.4byte	.LBB471-.Ltext0
	.4byte	.LBE471-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB474-.Ltext0
	.4byte	.LBE474-.Ltext0
	.4byte	.LBB475-.Ltext0
	.4byte	.LBE475-.Ltext0
	.4byte	.LBB476-.Ltext0
	.4byte	.LBE476-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB488-.Ltext0
	.4byte	.LBE488-.Ltext0
	.4byte	.LBB493-.Ltext0
	.4byte	.LBE493-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB520-.Ltext0
	.4byte	.LBE520-.Ltext0
	.4byte	.LBB528-.Ltext0
	.4byte	.LBE528-.Ltext0
	.4byte	.LBB529-.Ltext0
	.4byte	.LBE529-.Ltext0
	.4byte	.LBB530-.Ltext0
	.4byte	.LBE530-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB522-.Ltext0
	.4byte	.LBE522-.Ltext0
	.4byte	.LBB523-.Ltext0
	.4byte	.LBE523-.Ltext0
	.4byte	.LBB524-.Ltext0
	.4byte	.LBE524-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB541-.Ltext0
	.4byte	.LBE541-.Ltext0
	.4byte	.LBB542-.Ltext0
	.4byte	.LBE542-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB548-.Ltext0
	.4byte	.LBE548-.Ltext0
	.4byte	.LBB549-.Ltext0
	.4byte	.LBE549-.Ltext0
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
.LASF592:
	.ascii	"re_try_next_blk\000"
.LASF23:
	.ascii	"saved_command_line\000"
.LASF587:
	.ascii	"P2V_block_in_plane\000"
.LASF343:
	.ascii	"ftl_memcmp\000"
.LASF216:
	.ascii	"g_GlobalSysVersion\000"
.LASF264:
	.ascii	"g_totle_vendor_block\000"
.LASF96:
	.ascii	"BlkNum\000"
.LASF597:
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
.LASF663:
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
.LASF606:
	.ascii	"FtlBbmMapBadBlock\000"
.LASF604:
	.ascii	"FtlBbtInfoPrint\000"
.LASF55:
	.ascii	"hex_asc_upper\000"
.LASF461:
	.ascii	"saved_active_page\000"
.LASF505:
	.ascii	"re_save_vpndata\000"
.LASF535:
	.ascii	"region_num\000"
.LASF601:
	.ascii	"re_save_bbmdata\000"
.LASF621:
	.ascii	"first_lpa_sctidx\000"
.LASF20:
	.ascii	"__security_initcall_start\000"
.LASF307:
	.ascii	"g_totle_slc_erase_count\000"
.LASF595:
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
.LASF586:
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
.LASF613:
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
.LASF634:
	.ascii	"create_first_buffer_superblock\000"
.LASF63:
	.ascii	"p_data\000"
.LASF245:
	.ascii	"c_gc_page_buf_num\000"
.LASF653:
	.ascii	"limit\000"
.LASF514:
	.ascii	"Index\000"
.LASF224:
	.ascii	"gSysFreeQueue\000"
.LASF144:
	.ascii	"l2p_write_count\000"
.LASF620:
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
.LASF671:
	.ascii	"__builtin_strcpy\000"
.LASF290:
	.ascii	"g_gc_bad_block_gc_index\000"
.LASF269:
	.ascii	"p_free_data_block_list_head\000"
.LASF100:
	.ascii	"pBlkVpcTbl\000"
.LASF581:
	.ascii	"high\000"
.LASF636:
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
.LASF619:
	.ascii	"FtlRead\000"
.LASF272:
	.ascii	"g_num_free_superblocks\000"
.LASF591:
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
.LASF611:
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
.LASF648:
	.ascii	"FtlSysBlkNumInit\000"
.LASF657:
	.ascii	"memset\000"
.LASF328:
	.ascii	"TotleReadBufferCount1\000"
.LASF160:
	.ascii	"totle_read_sector\000"
.LASF330:
	.ascii	"TotleReadBufferCount3\000"
.LASF626:
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
.LASF641:
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
.LASF583:
	.ascii	"pagePreBlk\000"
.LASF129:
	.ascii	"activePageOffset\000"
.LASF77:
	.ascii	"reserved_blk\000"
.LASF482:
	.ascii	"FtlLoadMapInfo\000"
.LASF366:
	.ascii	"block_index\000"
.LASF650:
	.ascii	"FtlPrintInfo\000"
.LASF480:
	.ascii	"FtlReUsePrevPpa\000"
.LASF250:
	.ascii	"p_swl_mul_table\000"
.LASF564:
	.ascii	"insert_data_list\000"
.LASF644:
	.ascii	"FtlVariablesInit\000"
.LASF268:
	.ascii	"p_data_block_list_table\000"
.LASF512:
	.ascii	"ec_mod_count\000"
.LASF435:
	.ascii	"pNode\000"
.LASF304:
	.ascii	"g_totle_mlc_erase_count\000"
.LASF584:
	.ascii	"ftl_cmp_data_ver\000"
.LASF276:
	.ascii	"g_gc_temp_superblock\000"
.LASF261:
	.ascii	"p_vendor_block_valid_page_count\000"
.LASF499:
	.ascii	"FtlScanSysBlk\000"
.LASF667:
	.ascii	"l2p_addr_tran\000"
.LASF309:
	.ascii	"g_max_erase_count\000"
.LASF210:
	.ascii	"c_ftl_nand_init_sys_blks_per_plane\000"
.LASF182:
	.ascii	"ftl_gc_page_buffer\000"
.LASF233:
	.ascii	"g_req_cache\000"
.LASF647:
	.ascii	"FtlConstantsInit\000"
.LASF451:
	.ascii	"pages\000"
.LASF420:
	.ascii	"update_vpc_list\000"
.LASF660:
	.ascii	"rknand_print_hex\000"
.LASF408:
	.ascii	"p_gc_page\000"
.LASF15:
	.ascii	"elf_hwcap\000"
.LASF394:
	.ascii	"Ftl_get_new_temp_ppa\000"
.LASF136:
	.ascii	"PowerLostTimes\000"
.LASF572:
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
.LASF658:
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
.LASF574:
	.ascii	"FtlFreeSysBLkSort\000"
.LASF266:
	.ascii	"p_l2p_map_buf\000"
.LASF642:
	.ascii	"IsBlkInVendorPart\000"
.LASF598:
	.ascii	"FtlBbtMemInit\000"
.LASF244:
	.ascii	"gp_gc_page_buf_info\000"
.LASF167:
	.ascii	"front\000"
.LASF235:
	.ascii	"p_sys_data_buf\000"
.LASF93:
	.ascii	"index\000"
.LASF538:
	.ascii	"lastMapBlk\000"
.LASF623:
	.ascii	"last_lpa_nscts\000"
.LASF301:
	.ascii	"g_tmp_data_superblock_id\000"
.LASF247:
	.ascii	"gp_ect_tbl_info\000"
.LASF654:
	.ascii	"rknand_proc_ftlread\000"
.LASF542:
	.ascii	"ftl_free_no_use_map_blk\000"
.LASF665:
	.ascii	"/home/ldq/rk-linux/kernel-debug\000"
.LASF35:
	.ascii	"atomic_notifier_head\000"
.LASF384:
	.ascii	"FtlReadRefresh\000"
.LASF187:
	.ascii	"c_ftl_nand_blks_per_die\000"
.LASF565:
	.ascii	"node_data_count\000"
.LASF600:
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
.LASF670:
	.ascii	"strcpy\000"
.LASF61:
	.ascii	"status\000"
.LASF68:
	.ascii	"die_num\000"
.LASF627:
	.ascii	"sftl_init\000"
.LASF292:
	.ascii	"g_gc_free_blk_threshold\000"
.LASF32:
	.ascii	"kptr_restrict\000"
.LASF593:
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
.LASF630:
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
.LASF662:
	.ascii	"sprintf\000"
.LASF108:
	.ascii	"ftl_ect_tbl_info\000"
.LASF540:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
.LASF513:
	.ascii	"FtlVendorPartRead\000"
.LASF610:
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
.LASF602:
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
.LASF632:
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
.LASF599:
	.ascii	"FtlBbmTblFlush\000"
.LASF567:
	.ascii	"tmp_multiplier_value\000"
.LASF659:
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
.LASF609:
	.ascii	"FtlDiscard\000"
.LASF36:
	.ascii	"panic_notifier_list\000"
.LASF287:
	.ascii	"g_gc_next_blk\000"
.LASF386:
	.ascii	"add_count\000"
.LASF154:
	.ascii	"inkDie_write_and_check_en\000"
.LASF580:
	.ascii	"FtlGetLastWrittenPage\000"
.LASF465:
	.ascii	"detected_active_page\000"
.LASF509:
	.ascii	"FtlUpdateVaildLpn\000"
.LASF24:
	.ascii	"reset_devices\000"
.LASF157:
	.ascii	"refresh_enable_mode\000"
.LASF605:
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
.LASF664:
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
.LASF614:
	.ascii	"sctidx\000"
.LASF390:
	.ascii	"superBlk\000"
.LASF183:
	.ascii	"flag\000"
.LASF284:
	.ascii	"g_gc_page_offset\000"
.LASF640:
	.ascii	"test_mode\000"
.LASF299:
	.ascii	"g_totle_discard_page_count\000"
.LASF217:
	.ascii	"g_GlobalDataVersion\000"
.LASF651:
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
.LASF615:
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
.LASF645:
	.ascii	"FtlMemInit\000"
.LASF303:
	.ascii	"g_totle_l2p_write_count\000"
.LASF588:
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
.LASF643:
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
.LASF582:
	.ascii	"spareBuf\000"
.LASF188:
	.ascii	"c_ftl_nand_blks_per_die_shift\000"
.LASF140:
	.ascii	"ftl_sys_ext_info\000"
.LASF265:
	.ascii	"p_l2p_ram_map\000"
.LASF97:
	.ascii	"maxBlkNum\000"
.LASF661:
	.ascii	"ftl_malloc\000"
.LASF444:
	.ascii	"pDataHeader\000"
.LASF170:
	.ascii	"current_page\000"
.LASF81:
	.ascii	"erase_blk\000"
.LASF455:
	.ascii	"SupperBlkListInit\000"
.LASF633:
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
.LASF635:
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
.LASF578:
	.ascii	"FtlFreeSysBlkQueueEmpty\000"
.LASF194:
	.ascii	"c_ftl_nand_page_pre_slc_blk\000"
.LASF515:
	.ascii	"nSec\000"
.LASF596:
	.ascii	"FtlBbtCalcTotleCnt\000"
.LASF196:
	.ascii	"c_ftl_nand_sec_pre_page\000"
.LASF495:
	.ascii	"FtlSlcSuperblockCheck\000"
.LASF585:
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
.LASF622:
	.ascii	"first_lpa_nscts\000"
.LASF302:
	.ascii	"g_totle_cache_write_count\000"
.LASF496:
	.ascii	"ftl_set_blk_mode\000"
.LASF452:
	.ascii	"Ftl_load_ext_data\000"
.LASF506:
	.ascii	"FtlWriteDump_data\000"
.LASF637:
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
.LASF656:
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
.LASF631:
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
.LASF571:
	.ascii	"remalloc\000"
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
.LASF618:
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
.LASF628:
	.ascii	"pNand\000"
.LASF49:
	.ascii	"SYSTEM_RUNNING\000"
.LASF516:
	.ascii	"pBuf\000"
.LASF142:
	.ascii	"write_page_count\000"
.LASF603:
	.ascii	"bitmap\000"
.LASF200:
	.ascii	"c_ftl_nand_reserved_blks\000"
.LASF668:
	.ascii	"sftl_get_density\000"
.LASF639:
	.ascii	"test_page_step\000"
.LASF594:
	.ascii	"blkAddr\000"
.LASF286:
	.ascii	"g_gc_cur_blk_max_valid_pages\000"
.LASF288:
	.ascii	"g_gc_next_blk_1\000"
.LASF529:
	.ascii	"target_region\000"
.LASF612:
	.ascii	"end_lpa\000"
.LASF473:
	.ascii	"scan_completed\000"
.LASF655:
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
.LASF625:
	.ascii	"sftl_deinit\000"
.LASF486:
	.ascii	"pMapBlockInfo\000"
.LASF151:
	.ascii	"last_refresh_data_times\000"
.LASF666:
	.ascii	"system_states\000"
.LASF159:
	.ascii	"totle_write_sector\000"
.LASF229:
	.ascii	"req_prgm\000"
.LASF590:
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
.LASF669:
	.ascii	"memcpy\000"
.LASF504:
	.ascii	"prog_error_count\000"
.LASF336:
	.ascii	"gc_discard_updated\000"
.LASF412:
	.ascii	"prev_superblock_id\000"
.LASF638:
	.ascii	"test_page_num\000"
.LASF156:
	.ascii	"last_refresh_read_count\000"
.LASF478:
	.ascii	"lookup_ppa_ver\000"
.LASF490:
	.ascii	"totleBlkNum\000"
.LASF608:
	.ascii	"sftl_read\000"
.LASF577:
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
.LASF607:
	.ascii	"sftl_write\000"
.LASF118:
	.ascii	"ftl_map_blk_header\000"
.LASF579:
	.ascii	"FtlFreeSysBlkQueueInit\000"
.LASF25:
	.ascii	"late_time_init\000"
.LASF646:
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
.LASF624:
	.ascii	"sector\000"
.LASF507:
	.ascii	"num_page\000"
.LASF617:
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
.LASF652:
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
.LASF589:
	.ascii	"V2P_block\000"
.LASF115:
	.ascii	"sysBlksPerPlane\000"
.LASF454:
	.ascii	"Ftl_save_ext_data\000"
.LASF616:
	.ascii	"FtlCacheWriteBack\000"
.LASF79:
	.ascii	"nand_ops\000"
.LASF629:
	.ascii	"load_bbt\000"
.LASF649:
	.ascii	"blkNum\000"
.LASF367:
	.ascii	"page_index\000"
.LASF502:
	.ascii	"FtlVpcTblFlush\000"
.LASF109:
	.ascii	"hash\000"
.LASF573:
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
