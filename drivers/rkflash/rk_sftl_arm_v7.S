/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-07-14
 */
	.file	"rk_sftl.c"
	.text
.Ltext0:
	.global	__udivsi3
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_addr_tran, %function
l2p_addr_tran:
.LFB271:
	.file 1 "drivers/rkflash/rksftl/sftl_flash.c"
	.loc 1 18 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL0:
	mov	ip, sp
.LCFI0:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
.LCFI1:
	sub	fp, ip, #4
.LCFI2:
	.loc 1 25 0
	ldr	r3, .L3
	.loc 1 18 0
	mov	r8, r1
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
	lsr	r7, r6, #10
	.loc 1 37 0
	ubfx	r6, r6, #0, #10
.LVL4:
	.loc 1 27 0
	cmp	r3, #4
	.loc 1 34 0
	uxth	r0, r7
.LVL5:
	.loc 1 28 0
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
	uxtheq	r5, r5
.LVL8:
	.loc 1 34 0
	bl	__udivsi3
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
	mov	r0, #0
.LVL13:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L4:
	.align	2
.L3:
	.word	g_nand_phy_info
.LFE271:
	.size	l2p_addr_tran, .-l2p_addr_tran
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode.part.6, %function
ftl_set_blk_mode.part.6:
.LFB286:
	.file 2 "drivers/rkflash/rksftl/sftl_sys.c"
	.loc 2 1395 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL14:
	mov	ip, sp
.LCFI3:
	push	{fp, ip, lr, pc}
.LCFI4:
	sub	fp, ip, #4
.LCFI5:
	.loc 2 1398 0
	ldr	r3, .L6
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
.LVL15:
	ldr	r2, [r3]
	ldr	r3, [r2, r1, lsl #2]
	orr	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L7:
	.align	2
.L6:
	.word	.LANCHOR0
.LFE286:
	.size	ftl_set_blk_mode.part.6, .-ftl_set_blk_mode.part.6
	.align	2
	.global	Ftl_log2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
.LFB152:
	.file 3 "drivers/rkflash/rksftl/sftl_plat.c"
	.loc 3 127 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL16:
	mov	ip, sp
.LCFI6:
	push	{fp, ip, lr, pc}
.LCFI7:
	sub	fp, ip, #4
.LCFI8:
	.loc 3 131 0
	mov	r1, #0
	.loc 3 129 0
	mov	r2, #1
.LVL17:
.L9:
	.loc 3 131 0 discriminator 1
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L10
	.loc 3 133 0
	sub	r0, r3, #1
.LVL18:
	.loc 3 135 0
	uxth	r0, r0
	ldmfd	sp, {fp, sp, pc}
.LVL19:
.L10:
	.loc 3 132 0 discriminator 3
	lsl	r2, r2, #1
	b	.L9
.LFE152:
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo, %function
FtlPrintInfo:
.LFB153:
	.loc 3 159 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL20:
	mov	ip, sp
.LCFI9:
	push	{fp, ip, lr, pc}
.LCFI10:
	sub	fp, ip, #4
.LCFI11:
	ldmfd	sp, {fp, sp, pc}
.LFE153:
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	2
	.global	FtlSysBlkNumInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
.LFB154:
	.loc 3 165 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL21:
	.loc 3 165 0
	mov	ip, sp
.LCFI12:
	push	{fp, ip, lr, pc}
.LCFI13:
	sub	fp, ip, #4
.LCFI14:
	.loc 3 168 0
	ldr	r3, .L13
	.loc 3 165 0
	uxth	r0, r0
	.loc 3 169 0
	ldrh	r2, [r3, #8]
	cmp	r0, #24
	.loc 3 170 0
	ldrh	r1, [r3, #18]
	movcc	r0, #24
	.loc 3 168 0
	str	r0, [r3, #4]
	.loc 3 169 0
	mul	r2, r0, r2
	.loc 3 170 0
	sub	r0, r1, r0
.LVL22:
	.loc 3 171 0
	ldr	r1, [r3, #24]
	.loc 3 170 0
	strh	r0, [r3, #16]	@ movhi
	.loc 3 174 0
	mov	r0, #0
	.loc 3 169 0
	str	r2, [r3, #12]
	.loc 3 171 0
	sub	r2, r1, r2
	str	r2, [r3, #20]
	.loc 3 174 0
	ldmfd	sp, {fp, sp, pc}
.L14:
	.align	2
.L13:
	.word	.LANCHOR0
.LFE154:
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__divsi3
	.align	2
	.global	FtlConstantsInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
.LFB155:
	.loc 3 183 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL23:
	mov	ip, sp
.LCFI15:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI16:
	sub	fp, ip, #4
.LCFI17:
	sub	sp, sp, #4
	.loc 3 183 0
	mov	r7, r0
	.loc 3 191 0
	ldrh	r2, [r7, #14]
	.loc 3 188 0
	ldr	r4, .L25
	.loc 3 197 0
	ldr	ip, .L25+4
	.loc 3 191 0
	cmp	r2, #4
	.loc 3 188 0
	ldrh	r1, [r0]
	.loc 3 192 0
	ldrheq	r2, [r7, #6]
	.loc 3 189 0
	ldrh	r6, [r0, #2]
	.loc 3 190 0
	ldrh	r0, [r0, #4]
.LVL24:
	.loc 3 188 0
	strh	r1, [r4, #28]	@ movhi
	.loc 3 192 0
	lsreq	r2, r2, #1
	.loc 3 189 0
	strh	r6, [r4, #30]	@ movhi
	.loc 3 190 0
	strh	r0, [r4, #32]	@ movhi
	.loc 3 192 0
	strheq	r2, [r4, #18]	@ movhi
	.loc 3 193 0
	moveq	r2, #8
	strheq	r2, [r4, #34]	@ movhi
	.loc 3 183 0
	mov	r2, #0
.LVL25:
.L17:
	.loc 3 197 0 discriminator 3
	strb	r2, [r2, ip]
	.loc 3 196 0 discriminator 3
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L17
	.loc 3 205 0
	ldrh	r5, [r4, #18]
	.loc 3 204 0
	smulbb	r6, r6, r0
	.loc 3 198 0
	mov	r2, #5
.LVL26:
	.loc 3 200 0
	cmp	r1, #1
	.loc 3 202 0
	mov	ip, #640
	.loc 3 204 0
	uxth	r6, r6
	.loc 3 198 0
	strh	r2, [r4, #68]	@ movhi
	.loc 3 205 0
	smulbb	r0, r0, r5
	.loc 3 199 0
	mov	r2, #0
	.loc 3 201 0
	strheq	r1, [r4, #68]	@ movhi
	.loc 3 202 0
	strh	ip, [r4, #72]	@ movhi
	.loc 3 205 0
	uxth	r0, r0
	.loc 3 202 0
	str	ip, [fp, #-44]
	.loc 3 199 0
	strh	r2, [r4, #70]	@ movhi
	.loc 3 204 0
	strh	r6, [r4, #8]	@ movhi
	.loc 3 205 0
	strh	r0, [r4, #74]	@ movhi
	.loc 3 206 0
	bl	Ftl_log2
.LVL27:
	.loc 3 207 0
	ldrh	r9, [r7, #12]
	.loc 3 210 0
	ldrh	r10, [r4, #34]
	.loc 3 206 0
	strh	r0, [r4, #76]	@ movhi
	.loc 3 207 0
	strh	r9, [r4, #78]	@ movhi
	.loc 3 209 0
	smulbb	r2, r6, r9
	.loc 3 210 0
	mov	r0, r10
	.loc 3 208 0
	strh	r9, [r4, #80]	@ movhi
	.loc 3 209 0
	strh	r2, [r4, #82]	@ movhi
	.loc 3 210 0
	bl	Ftl_log2
.LVL28:
	.loc 3 214 0
	ldrh	r3, [r7, #20]
	.loc 3 212 0
	lsl	r2, r10, #9
	.loc 3 210 0
	mov	r8, r0
	strh	r0, [r4, #84]	@ movhi
	.loc 3 212 0
	uxth	r2, r2
	.loc 3 218 0
	mul	r1, r9, r10
	.loc 3 214 0
	strh	r3, [r4, #90]	@ movhi
	.loc 3 218 0
	mov	r0, #5120
	.loc 3 216 0
	mul	r3, r5, r6
	.loc 3 212 0
	strh	r2, [r4, #86]	@ movhi
	.loc 3 213 0
	lsr	r2, r2, #8
	.loc 3 223 0
	lsl	r5, r5, #6
	.loc 3 213 0
	strh	r2, [r4, #88]	@ movhi
	.loc 3 216 0
	str	r3, [r4, #24]
	.loc 3 217 0
	mul	r3, r10, r3
	mul	r3, r9, r3
	asr	r3, r3, #11
	str	r3, [r4, #92]
	.loc 3 218 0
	bl	__divsi3
.LVL29:
	ldr	ip, [fp, #-44]
	uxth	r0, r0
	.loc 3 226 0
	mov	r1, r6
	.loc 3 219 0
	cmp	r0, #4
	.loc 3 220 0
	movls	r3, #4
	.loc 3 218 0
	strhhi	r0, [r4, #96]	@ movhi
	.loc 3 221 0
	asr	r7, ip, r8
.LVL30:
	.loc 3 223 0
	add	r8, r8, #9
	asr	r5, r5, r8
	.loc 3 220 0
	strhls	r3, [r4, #96]	@ movhi
	.loc 3 221 0
	add	r7, r7, #2
	.loc 3 226 0
	ldrh	r0, [r4, #96]
	.loc 3 223 0
	strh	r5, [r4, #100]	@ movhi
	uxth	r5, r5
	.loc 3 221 0
	strh	r7, [r4, #98]	@ movhi
	.loc 3 224 0
	mul	r3, r6, r5
	.loc 3 226 0
	add	r5, r5, #8
	.loc 3 224 0
	str	r3, [r4, #104]
	.loc 3 226 0
	bl	__udivsi3
.LVL31:
	uxtah	r0, r5, r0
	.loc 3 227 0
	cmp	r6, #1
	.loc 3 228 0
	addeq	r0, r0, #4
	str	r0, [r4, #4]
	.loc 3 231 0
	ldrh	r0, [r4, #4]
	bl	FtlSysBlkNumInit
.LVL32:
	.loc 3 232 0
	ldr	r3, [r4, #4]
	.loc 3 236 0
	mov	r0, #0
	str	r0, [r4, #116]
	.loc 3 232 0
	str	r3, [r4, #108]
	.loc 3 234 0
	ldr	r3, [r4, #20]
	lsl	r2, r3, #2
	ldrh	r3, [r4, #78]
	mul	r3, r3, r2
	ldrh	r2, [r4, #84]
	add	r2, r2, #9
	lsr	r3, r3, r2
	add	r3, r3, #2
	strh	r3, [r4, #112]	@ movhi
	.loc 3 235 0
	mov	r3, #32
	strh	r3, [r4, #114]	@ movhi
	.loc 3 237 0
	ldrh	r3, [r4, #96]
	add	r3, r3, #3
	strh	r3, [r4, #96]	@ movhi
	.loc 3 238 0
	ldr	r3, [r4, #104]
	add	r3, r3, #3
	str	r3, [r4, #104]
	.loc 3 263 0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.word	.LANCHOR0+36
.LFE155:
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
.LFB158:
	.loc 3 514 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL33:
	mov	ip, sp
.LCFI18:
	push	{fp, ip, lr, pc}
.LCFI19:
	sub	fp, ip, #4
.LCFI20:
	.loc 3 517 0
	ldr	r2, .L33
	.loc 3 514 0
	uxth	r1, r0
.LVL34:
	.loc 3 517 0
	ldrh	r0, [r2, #120]
	cmp	r0, #0
.LBB222:
.LBB223:
	.loc 3 519 0
	ldrne	r3, [r2, #124]
	ldrhne	r2, [r2, #96]
	addne	r2, r3, r2, lsl #1
.LBE223:
.LBE222:
	.loc 3 517 0
	bne	.L29
	ldmfd	sp, {fp, sp, pc}
.L30:
.LBB226:
.LBB224:
	.loc 3 519 0
	ldrh	r0, [r3], #2
	cmp	r1, r0
	beq	.L32
.L29:
	.loc 3 518 0
	cmp	r3, r2
	bne	.L30
.LBE224:
.LBE226:
	.loc 3 524 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L32:
.LBB227:
.LBB225:
	.loc 3 520 0
	mov	r0, #1
.LBE225:
.LBE227:
	.loc 3 525 0
	ldmfd	sp, {fp, sp, pc}
.L34:
	.align	2
.L33:
	.word	.LANCHOR0
.LFE158:
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	sftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
.LFB171:
	.loc 3 1214 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI21:
	push	{fp, ip, lr, pc}
.LCFI22:
	sub	fp, ip, #4
.LCFI23:
	.loc 3 1216 0
	ldr	r3, .L36
	ldr	r0, [r3, #116]
	ldmfd	sp, {fp, sp, pc}
.L37:
	.align	2
.L36:
	.word	.LANCHOR0
.LFE171:
	.size	sftl_get_density, .-sftl_get_density
	.global	__umodsi3
	.align	2
	.global	FtlBbmMapBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
.LFB172:
	.file 4 "drivers/rkflash/rksftl/sftl_bbm.c"
	.loc 4 5 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL35:
	mov	ip, sp
.LCFI24:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI25:
	sub	fp, ip, #4
.LCFI26:
	sub	sp, sp, #4
	.loc 4 5 0
	uxth	r5, r0
.LVL36:
	.loc 4 10 0
	ldr	r4, .L39
	mov	r0, r5
	ldrh	r7, [r4, #74]
	mov	r1, r7
	bl	__udivsi3
.LVL37:
	uxth	r6, r0
.LVL38:
	mov	r1, r7
	mov	r0, r5
	bl	__umodsi3
.LVL39:
	.loc 4 13 0
	add	r2, r4, r6, lsl #2
	uxth	r3, r0
	.loc 4 15 0
	ldr	r2, [r2, #156]
	lsr	r0, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	ldr	r1, [r2, r0, lsl #2]
	orr	r1, r1, lr, lsl ip
	str	r1, [r2, r0, lsl #2]
	.loc 4 16 0
	mov	r2, r6
	str	r1, [sp]
	mov	r1, r5
	ldr	r0, .L39+4
	bl	printk
.LVL40:
	.loc 4 17 0
	ldrh	r3, [r4, #134]
	.loc 4 19 0
	mov	r0, #0
	.loc 4 17 0
	add	r3, r3, #1
	strh	r3, [r4, #134]	@ movhi
	.loc 4 19 0
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L40:
	.align	2
.L39:
	.word	.LANCHOR0
	.word	.LC0
.LFE172:
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
.LFB173:
	.loc 4 24 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL41:
	mov	ip, sp
.LCFI27:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI28:
	sub	fp, ip, #4
.LCFI29:
	.loc 4 30 0
	ldr	r5, .L42
	.loc 4 24 0
	uxth	r6, r0
.LVL42:
	.loc 4 30 0
	ldrh	r7, [r5, #74]
	.loc 4 31 0
	mov	r0, r6
	mov	r1, r7
	bl	__umodsi3
.LVL43:
	.loc 4 30 0
	mov	r1, r7
	.loc 4 31 0
	uxth	r4, r0
.LVL44:
	.loc 4 30 0
	mov	r0, r6
	bl	__udivsi3
.LVL45:
	uxth	r0, r0
.LVL46:
	.loc 4 34 0
	lsr	r2, r4, #5
	.loc 4 33 0
	add	r5, r5, r0, lsl #2
	.loc 4 34 0
	and	r4, r4, #31
.LVL47:
	ldr	r3, [r5, #156]
	ldr	r0, [r3, r2, lsl #2]
.LVL48:
	lsr	r0, r0, r4
	.loc 4 36 0
	and	r0, r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L43:
	.align	2
.L42:
	.word	.LANCHOR0
.LFE173:
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
.LFB174:
	.loc 4 39 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI30:
	push	{fp, ip, lr, pc}
.LCFI31:
	sub	fp, ip, #4
.LCFI32:
	ldmfd	sp, {fp, sp, pc}
.LFE174:
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	2
	.global	FtlBbtMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
.LFB177:
	.loc 4 149 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.loc 4 149 0
	mov	ip, sp
.LCFI33:
	push	{fp, ip, lr, pc}
.LCFI34:
	sub	fp, ip, #4
.LCFI35:
	.loc 4 150 0
	ldr	r0, .L46
	mvn	r3, #0
.LBB233:
.LBB234:
.LBB235:
	.loc 1 202 0
	mov	r2, #16
	mov	r1, #255
.LBE235:
.LBE234:
.LBE233:
	.loc 4 150 0
	strh	r3, [r0, #128]	@ movhi
	.loc 4 151 0
	mov	r3, #0
	strh	r3, [r0, #134]	@ movhi
.LBB238:
.LBB237:
.LBB236:
	.loc 1 202 0
	add	r0, r0, #140
	bl	memset
.LVL49:
	ldmfd	sp, {fp, sp, pc}
.L47:
	.align	2
.L46:
	.word	.LANCHOR0
.LBE236:
.LBE237:
.LBE238:
.LFE177:
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
.LFB178:
	.loc 4 157 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI36:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI37:
	sub	fp, ip, #4
.LCFI38:
	.loc 4 160 0
	ldr	r3, .L55
	mov	r5, #0
	.loc 4 159 0
	mov	r4, r5
	.loc 4 160 0
	ldrh	r2, [r3, #74]
	ldrh	r6, [r3, #30]
	mul	r6, r6, r2
.L49:
	.loc 4 160 0 is_stmt 0 discriminator 1
	uxth	r0, r5
	cmp	r0, r6
	blt	.L51
	.loc 4 169 0 is_stmt 1
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L51:
	.loc 4 162 0
	bl	FtlBbmIsBadBlock
.LVL50:
	cmp	r0, #0
	add	r5, r5, #1
	.loc 4 164 0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L49
.L56:
	.align	2
.L55:
	.word	.LANCHOR0
.LFE178:
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
.LFB182:
	.loc 2 5 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL51:
	mov	ip, sp
.LCFI39:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI40:
	sub	fp, ip, #4
.LCFI41:
	.loc 2 8 0
	ldr	r4, .L58
	.loc 2 5 0
	uxth	r7, r0
.LVL52:
	uxth	r5, r1
.LVL53:
	.loc 2 8 0
	ldrh	r6, [r4, #32]
	mov	r0, r7
	mov	r1, r6
	bl	__udivsi3
.LVL54:
	ldrh	r4, [r4, #74]
	smulbb	r5, r6, r5
.LVL55:
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__umodsi3
.LVL56:
	add	r0, r5, r0
	add	r0, r4, r0
	.loc 2 10 0
	uxth	r0, r0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L59:
	.align	2
.L58:
	.word	.LANCHOR0
.LFE182:
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
.LFB183:
	.loc 2 13 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL57:
	mov	ip, sp
.LCFI42:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI43:
	sub	fp, ip, #4
.LCFI44:
	.loc 2 16 0
	ldr	r3, .L61
	.loc 2 13 0
	uxth	r5, r0
.LVL58:
	.loc 2 16 0
	ldrh	r6, [r3, #32]
	mov	r0, r5
	ldrh	r1, [r3, #74]
	bl	__udivsi3
.LVL59:
	mov	r1, r6
	smulbb	r4, r0, r6
	mov	r0, r5
	bl	__umodsi3
.LVL60:
	add	r0, r4, r0
	.loc 2 18 0
	uxth	r0, r0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L62:
	.align	2
.L61:
	.word	.LANCHOR0
.LFE183:
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
.LFB184:
	.loc 2 21 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL61:
	mov	ip, sp
.LCFI45:
	push	{r4, fp, ip, lr, pc}
.LCFI46:
	sub	fp, ip, #4
.LCFI47:
	.loc 2 24 0
	ldr	r4, .L64
	uxth	r0, r0
.LVL62:
	ldrh	r1, [r4, #74]
	bl	__umodsi3
.LVL63:
	.loc 2 25 0
	ldrh	r1, [r4, #32]
	uxth	r0, r0
	bl	__udivsi3
.LVL64:
	.loc 2 27 0
	uxth	r0, r0
	ldmfd	sp, {r4, fp, sp, pc}
.L65:
	.align	2
.L64:
	.word	.LANCHOR0
.LFE184:
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
.LFB185:
	.loc 2 31 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL65:
	mov	ip, sp
.LCFI48:
	push	{fp, ip, lr, pc}
.LCFI49:
	sub	fp, ip, #4
.LCFI50:
	.loc 2 34 0
	cmp	r0, r1
	bls	.L67
	.loc 2 36 0
	sub	r0, r0, r1
.LVL66:
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.LVL67:
.L67:
	.loc 2 40 0
	sub	r0, r1, r0
.LVL68:
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	.loc 2 44 0
	ldmfd	sp, {fp, sp, pc}
.LFE185:
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
.LFB187:
	.loc 2 76 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL69:
	mov	ip, sp
.LCFI51:
	push	{r4, fp, ip, lr, pc}
.LCFI52:
	sub	fp, ip, #4
.LCFI53:
	.loc 2 79 0
	ldr	r3, .L70
	mov	r4, #0
.LBB244:
.LBB245:
.LBB246:
	.loc 1 202 0
	mov	r1, #2048
.LBE246:
.LBE245:
.LBE244:
	.loc 2 82 0
	strh	r0, [r3, #188]	@ movhi
.LBB251:
.LBB249:
.LBB247:
	.loc 1 202 0
	add	r0, r3, #196
.LVL70:
.LBE247:
.LBE249:
.LBE251:
	.loc 2 79 0
	strh	r4, [r3, #190]	@ movhi
	.loc 2 80 0
	strh	r4, [r3, #192]	@ movhi
	.loc 2 81 0
	strh	r4, [r3, #194]	@ movhi
.LBB252:
.LBB250:
.LBB248:
	.loc 1 202 0
	bl	__memzero
.LVL71:
.LBE248:
.LBE250:
.LBE252:
	.loc 2 85 0
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L71:
	.align	2
.L70:
	.word	.LANCHOR0
.LFE187:
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
.LFB188:
	.loc 2 88 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI54:
	push	{fp, ip, lr, pc}
.LCFI55:
	sub	fp, ip, #4
.LCFI56:
	.loc 2 89 0
	ldr	r3, .L73
	ldrh	r0, [r3, #194]
	.loc 2 90 0
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L74:
	.align	2
.L73:
	.word	.LANCHOR0
.LFE188:
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
.LFB189:
	.loc 2 93 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI57:
	push	{fp, ip, lr, pc}
.LCFI58:
	sub	fp, ip, #4
.LCFI59:
	.loc 2 94 0
	ldr	r3, .L76
	ldrh	r0, [r3, #194]
	.loc 2 95 0
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L77:
	.align	2
.L76:
	.word	.LANCHOR0
.LFE189:
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
.LFB191:
	.loc 2 118 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI60:
	push	{r4, fp, ip, lr, pc}
.LCFI61:
	sub	fp, ip, #4
.LCFI62:
	.loc 2 123 0
	ldr	r3, .L87
	ldrh	r2, [r3, #194]
	cmp	r2, #0
	ldmfdeq	sp, {r4, fp, sp, pc}
	.loc 2 119 0
	ldr	r2, .L87+4
	mov	r4, #0
	ldrh	r1, [r3, #190]
	mov	lr, r4
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #192]
	and	ip, ip, #31
.LVL72:
.L80:
	.loc 2 124 0 discriminator 1
	uxth	r0, r4
	add	r4, r4, #1
	cmp	ip, r0
	bgt	.L81
	cmp	lr, #0
	strhne	r1, [r3, #190]	@ movhi
	strhne	r2, [r3, #192]	@ movhi
	ldmfd	sp, {r4, fp, sp, pc}
.L81:
	.loc 2 125 0 discriminator 3
	add	r0, r3, r1, lsl #1
	.loc 2 126 0 discriminator 3
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	.loc 2 125 0 discriminator 3
	ldrh	lr, [r0, #196]
.LVL73:
	.loc 2 127 0 discriminator 3
	add	r0, r3, r2, lsl #1
	strh	lr, [r0, #196]	@ movhi
	mov	lr, #1
.LVL74:
	.loc 2 128 0 discriminator 3
	add	r2, r2, lr
	ubfx	r2, r2, #0, #10
	b	.L80
.L88:
	.align	2
.L87:
	.word	.LANCHOR0
	.word	.LANCHOR0+2244
.LFE191:
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
.LFB192:
	.loc 2 163 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL75:
	mov	ip, sp
.LCFI63:
	push	{fp, ip, lr, pc}
.LCFI64:
	sub	fp, ip, #4
.LCFI65:
.LBB253:
.LBB254:
	.loc 2 94 0
	ldr	r3, .L95
.LBE254:
.LBE253:
	.loc 2 163 0
	uxth	r0, r0
.LBB256:
.LBB255:
	.loc 2 94 0
	ldrh	ip, [r3, #194]
.LBE255:
.LBE256:
	.loc 2 168 0
	cmp	ip, #1024
	.loc 2 171 0
	ldrhne	lr, [r3, #190]
	movne	r1, #0
.LVL76:
	.loc 2 168 0
	bne	.L91
.L93:
	.loc 2 164 0
	mov	r0, #0
.LVL77:
	ldmfd	sp, {fp, sp, pc}
.LVL78:
.L92:
	.loc 2 171 0
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #196]
	cmp	r2, r0
	beq	.L94
	.loc 2 170 0 discriminator 2
	add	r1, r1, #1
.L91:
	.loc 2 170 0 is_stmt 0 discriminator 1
	cmp	r1, ip
	bcc	.L92
	b	.L93
.L94:
	.loc 2 172 0 is_stmt 1
	mov	r0, #1
.LVL79:
	.loc 2 178 0
	ldmfd	sp, {fp, sp, pc}
.L96:
	.align	2
.L95:
	.word	.LANCHOR0
.LFE192:
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
.LFB196:
	.loc 2 249 0
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL80:
	mov	ip, sp
.LCFI66:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI67:
	sub	fp, ip, #4
.LCFI68:
	sub	sp, sp, #12
	.loc 2 249 0
	uxth	r0, r0
	.loc 2 260 0
	ldr	r1, .L113
	ldrh	r3, [r1, #16]
	mov	lr, r1
	cmp	r3, r0
	bls	.L99
.LBB259:
.LBB260:
	.loc 2 265 0
	ldr	r6, [r1, #2292]
	lsl	r5, r0, #3
	.loc 2 266 0
	mvn	ip, #0
	.loc 2 265 0
	add	r2, r6, r5
.LVL81:
	.loc 2 266 0
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	.loc 2 268 0
	ldr	r3, [r1, #2296]
.LVL82:
	cmp	r3, #0
	bne	.L100
.LVL83:
.L112:
	.loc 2 317 0
	str	r2, [r1, #2296]
	b	.L99
.LVL84:
.L100:
	.loc 2 273 0
	ldr	r7, [r1, #2300]
	lsl	r4, r0, #1
	.loc 2 274 0
	ldrh	r1, [r2, #4]
	.loc 2 279 0
	ldr	r10, [lr, #2292]
	.loc 2 273 0
	ldrh	r8, [r7, r4]
.LVL85:
	.loc 2 275 0
	cmp	r1, #0
	.loc 2 276 0
	streq	ip, [fp, #-44]
	.loc 2 274 0
	mulne	r1, r1, r8
.LVL86:
	.loc 2 282 0
	ldrh	r8, [lr, #16]
.LVL87:
	str	r8, [fp, #-48]
	.loc 2 294 0
	ldr	r8, [lr, #2304]
	.loc 2 274 0
	strne	r1, [fp, #-44]
	.loc 2 279 0
	sub	r1, r3, r10
.LVL88:
	ubfx	r1, r1, #3, #16
	.loc 2 294 0
	add	lr, r8, r4
	str	lr, [fp, #-52]
	.loc 2 256 0
	mov	lr, #0
.LVL89:
.L108:
	.loc 2 282 0
	ldr	r4, [fp, #-48]
	.loc 2 281 0
	add	lr, lr, #1
.LVL90:
	uxth	lr, lr
.LVL91:
	.loc 2 282 0
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	.loc 2 285 0
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L99
	.loc 2 288 0
	lsl	r9, r1, #1
	ldrh	r4, [r7, r9]
.LVL92:
	mov	ip, r4
	.loc 2 289 0
	ldrh	r4, [r3, #4]
.LVL93:
	.loc 2 290 0
	cmp	r4, #0
	.loc 2 289 0
	mulne	r4, r4, ip
.LVL94:
	.loc 2 293 0
	ldr	ip, [fp, #-44]
.LVL95:
	.loc 2 291 0
	mvneq	r4, #0
	.loc 2 293 0
	cmp	ip, r4
	bne	.L104
	.loc 2 294 0
	ldr	ip, [fp, #-52]
.LVL96:
	ldrh	r9, [r8, r9]
	ldrh	r4, [ip]
.LVL97:
	cmp	r9, r4
	bcc	.L106
.LVL98:
.L105:
	.loc 2 312 0
	strh	r1, [r6, r5]	@ movhi
	.loc 2 313 0
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	.loc 2 315 0
	ldr	r1, .L113
	ldr	ip, [r1, #2296]
.LVL99:
	cmp	r3, ip
	bne	.L109
	.loc 2 316 0
	strh	r0, [r3, #2]	@ movhi
	b	.L112
.LVL100:
.L104:
	.loc 2 298 0
	bcc	.L105
.L106:
	.loc 2 301 0
	ldrh	r4, [r3]
.LVL101:
	movw	ip, #65535
.LVL102:
	cmp	r4, ip
	bne	.L107
	.loc 2 302 0
	strh	r1, [r2, #2]	@ movhi
	.loc 2 303 0
	strh	r0, [r3]	@ movhi
	.loc 2 304 0
	ldr	r3, .L113
.LVL103:
	str	r2, [r3, #2308]
.LVL104:
.L99:
.LBE260:
.LBE259:
	.loc 2 323 0
	mov	r0, #0
.LVL105:
	sub	sp, fp, #40
.LCFI69:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL106:
.L107:
.LCFI70:
.LBB262:
.LBB261:
	.loc 2 309 0
	add	r3, r10, r4, lsl #3
	mov	r1, r4
	b	.L108
.LVL107:
.L109:
	.loc 2 319 0
	ldrh	r2, [r3, #2]
.LVL108:
	ldr	r1, [r1, #2292]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	.loc 2 320 0
	strh	r0, [r3, #2]	@ movhi
	b	.L99
.L114:
	.align	2
.L113:
	.word	.LANCHOR0
.LBE261:
.LBE262:
.LFE196:
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
.LFB195:
	.loc 2 218 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL109:
	mov	ip, sp
.LCFI71:
	push	{fp, ip, lr, pc}
.LCFI72:
	sub	fp, ip, #4
.LCFI73:
	.loc 2 219 0
	uxth	r0, r0
.LVL110:
	bl	insert_data_list
.LVL111:
	.loc 2 220 0
	ldr	r2, .L117
	movw	r1, #2312
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	.loc 2 221 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
.LBB265:
.LBB266:
	mov	r2, #221
	ldr	r1, .L117+4
	ldr	r0, .L117+8
	bl	printk
.LVL112:
	ldmfd	sp, {fp, sp, pc}
.L118:
	.align	2
.L117:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC1
.LBE266:
.LBE265:
.LFE195:
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
.LFB197:
	.loc 2 326 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL113:
	mov	ip, sp
.LCFI74:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
.LCFI75:
	sub	fp, ip, #4
.LCFI76:
	.loc 2 326 0
	uxth	r0, r0
	.loc 2 332 0
	movw	lr, #65535
	cmp	r0, lr
	beq	.L120
	.loc 2 335 0
	ldr	r2, .L126
	lsl	r5, r0, #3
	.loc 2 336 0
	mvn	r3, #0
	.loc 2 335 0
	ldr	r6, [r2, #2292]
	mov	r1, r2
	add	ip, r6, r5
.LVL114:
	.loc 2 336 0
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	.loc 2 338 0
	ldr	r3, [r2, #2316]
.LVL115:
	cmp	r3, #0
	.loc 2 339 0
	streq	ip, [r2, #2316]
	.loc 2 338 0
	beq	.L120
	.loc 2 343 0
	ldr	r8, [r2, #2304]
	lsl	r2, r0, #1
	.loc 2 345 0
	ldr	r7, [r1, #2292]
	.loc 2 352 0
	mov	r4, lr
	.loc 2 343 0
	ldrh	r9, [r8, r2]
.LVL116:
	.loc 2 345 0
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L124:
	.loc 2 348 0
	lsl	lr, r2, #1
	.loc 2 349 0
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L122
	.loc 2 352 0
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L123
	.loc 2 353 0
	strh	r2, [ip, #2]	@ movhi
	.loc 2 354 0
	strh	r0, [r3]	@ movhi
.L120:
	.loc 2 372 0
	mov	r0, #0
.LVL117:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL118:
.L123:
	.loc 2 358 0
	add	r3, r7, lr, lsl #3
	.loc 2 348 0
	mov	r2, lr
	b	.L124
.L122:
	.loc 2 361 0
	ldrh	lr, [r3, #2]
	strh	lr, [ip, #2]	@ movhi
	.loc 2 362 0
	strh	r2, [r6, r5]	@ movhi
	.loc 2 364 0
	ldr	r2, [r1, #2316]
	cmp	r3, r2
	.loc 2 368 0
	ldrhne	r2, [r3, #2]
	ldrne	r1, [r1, #2292]
	.loc 2 365 0
	strheq	r0, [r3, #2]	@ movhi
	.loc 2 366 0
	streq	ip, [r1, #2316]
	.loc 2 368 0
	lslne	r2, r2, #3
	strhne	r0, [r1, r2]	@ movhi
	.loc 2 369 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L120
.L127:
	.align	2
.L126:
	.word	.LANCHOR0
.LFE197:
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
.LFB194:
	.loc 2 211 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL119:
	mov	ip, sp
.LCFI77:
	push	{fp, ip, lr, pc}
.LCFI78:
	sub	fp, ip, #4
.LCFI79:
	.loc 2 212 0
	uxth	r0, r0
.LVL120:
	bl	insert_free_list
.LVL121:
	.loc 2 213 0
	ldr	r2, .L130
	add	r1, r2, #2320
	.loc 2 214 0
	ldrh	r2, [r2, #16]
	.loc 2 213 0
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	.loc 2 214 0
	cmp	r2, r3
	.loc 2 213 0
	strh	r3, [r1]	@ movhi
	.loc 2 214 0
	ldmfdcs	sp, {fp, sp, pc}
.LBB269:
.LBB270:
	mov	r2, #214
	ldr	r1, .L130+4
	ldr	r0, .L130+8
	bl	printk
.LVL122:
	ldmfd	sp, {fp, sp, pc}
.L131:
	.align	2
.L130:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC1
.LBE270:
.LBE269:
.LFE194:
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
.LFB198:
	.loc 2 375 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL123:
	mov	ip, sp
.LCFI80:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI81:
	sub	fp, ip, #4
.LCFI82:
	.loc 2 378 0
	ldr	r4, .L139
	.loc 2 375 0
	uxth	r1, r1
	.loc 2 379 0
	movw	r3, #65535
	.loc 2 375 0
	mov	r8, r0
	.loc 2 378 0
	ldr	r7, [r4, #2292]
	lsl	r6, r1, #3
	add	r5, r7, r6
.LVL124:
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
	ldr	r1, .L139+4
.LVL125:
	ldr	r0, .L139+8
	bl	printk
.LVL126:
.L133:
	.loc 2 381 0 is_stmt 1
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L134
	.loc 2 382 0
	cmp	r2, r3
	.loc 2 385 0
	ldrne	r3, [r4, #2292]
	.loc 2 383 0
	moveq	r3, #0
	streq	r3, [r8]
	.loc 2 385 0
	addne	r2, r3, r2, lsl #3
	.loc 2 386 0
	mvnne	r3, #0
	.loc 2 385 0
	strne	r2, [r8]
	.loc 2 386 0
	strhne	r3, [r2, #2]	@ movhi
.L136:
	.loc 2 399 0
	mvn	r3, #0
	.loc 2 401 0
	mov	r0, #0
	.loc 2 399 0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	.loc 2 401 0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L134:
	.loc 2 388 0
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L137
	.loc 2 389 0
	cmp	r3, r2
	.loc 2 391 0
	ldrne	r2, [r4, #2292]
	lslne	r3, r3, #3
	mvnne	r1, #0
.LVL127:
	.loc 2 389 0
	beq	.L136
.L138:
	.loc 2 397 0
	strh	r1, [r2, r3]	@ movhi
	b	.L136
.LVL128:
.L137:
	.loc 2 395 0
	ldr	r1, [r4, #2292]
.LVL129:
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	.loc 2 396 0
	ldrh	r3, [r5, #2]
	.loc 2 397 0
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2292]
	lsl	r3, r3, #3
	b	.L138
.L140:
	.align	2
.L139:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC1
.LFE198:
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
.LFB199:
	.loc 2 404 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL130:
	mov	ip, sp
.LCFI83:
	push	{r4, fp, ip, lr, pc}
.LCFI84:
	sub	fp, ip, #4
.LCFI85:
	.loc 2 408 0
	ldr	r3, [r0]
.LVL131:
	.loc 2 404 0
	uxth	r1, r1
	.loc 2 408 0
	cmp	r3, #0
	.loc 2 409 0
	movweq	r4, #65535
	.loc 2 408 0
	beq	.L141
	.loc 2 413 0
	ldr	r2, .L148
	.loc 2 412 0
	movw	ip, #65535
	.loc 2 413 0
	ldr	r4, [r2, #2292]
.L143:
	.loc 2 412 0
	cmp	r1, #0
	bne	.L144
.L146:
	.loc 2 416 0
	sub	r3, r3, r4
.LVL132:
	ubfx	r4, r3, #3, #16
	.loc 2 417 0
	mov	r1, r4
.LVL133:
	bl	List_remove_node
.LVL134:
.L141:
	.loc 2 420 0
	mov	r0, r4
.LVL135:
	ldmfd	sp, {r4, fp, sp, pc}
.LVL136:
.L144:
	.loc 2 412 0 discriminator 1
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L146
	.loc 2 414 0
	sub	r1, r1, #1
.LVL137:
	.loc 2 413 0
	add	r3, r4, r2, lsl #3
	.loc 2 414 0
	uxth	r1, r1
.LVL138:
	b	.L143
.L149:
	.align	2
.L148:
	.word	.LANCHOR0
.LFE199:
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_head_node, %function
List_pop_head_node:
.LFB200:
	.loc 2 423 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL139:
	mov	ip, sp
.LCFI86:
	push	{fp, ip, lr, pc}
.LCFI87:
	sub	fp, ip, #4
.LCFI88:
	.loc 2 424 0
	mov	r1, #0
	bl	List_pop_index_node
.LVL140:
	.loc 2 425 0
	ldmfd	sp, {fp, sp, pc}
.LFE200:
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
.LFB201:
	.loc 2 428 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL141:
	mov	ip, sp
.LCFI89:
	push	{fp, ip, lr, pc}
.LCFI90:
	sub	fp, ip, #4
.LCFI91:
	.loc 2 432 0
	ldr	r1, .L157
	.loc 2 428 0
	uxth	r2, r0
.LVL142:
	.loc 2 432 0
	ldr	r3, [r1, #2296]
.LVL143:
	cmp	r3, #0
	.loc 2 437 0
	ldrne	r1, [r1, #2292]
	.loc 2 436 0
	movwne	r0, #65535
	.loc 2 432 0
	bne	.L153
	.loc 2 433 0
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.LVL144:
.L155:
	.loc 2 438 0
	sub	r2, r2, #1
.LVL145:
	.loc 2 437 0
	add	r3, r1, r3, lsl #3
.LVL146:
	.loc 2 438 0
	uxth	r2, r2
.LVL147:
.L153:
	.loc 2 436 0
	cmp	r2, #0
	beq	.L154
	.loc 2 436 0 is_stmt 0 discriminator 1
	ldrh	r3, [r3]
.LVL148:
	cmp	r3, r0
	bne	.L155
	ldmfd	sp, {fp, sp, pc}
.LVL149:
.L154:
	.loc 2 442 0 is_stmt 1
	sub	r3, r3, r1
.LVL150:
	ubfx	r0, r3, #3, #16
	.loc 2 445 0
	ldmfd	sp, {fp, sp, pc}
.L158:
	.align	2
.L157:
	.word	.LANCHOR0
.LFE201:
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
.LFB202:
	.loc 2 448 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL151:
	mov	ip, sp
.LCFI92:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI93:
	sub	fp, ip, #4
.LCFI94:
	.loc 2 457 0
	ldr	r4, .L172
	movw	r3, #2324
	.loc 2 448 0
	uxth	r5, r0
.LVL152:
	.loc 2 457 0
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L161
	.loc 2 457 0 is_stmt 0 discriminator 1
	movw	r3, #2372
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L161
	.loc 2 457 0 discriminator 2
	movw	r3, #2420
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L161
.LBB273:
.LBB274:
	.loc 2 460 0 is_stmt 1
	ldr	r8, [r4, #2292]
	.loc 2 462 0
	ldr	r3, [r4, #2296]
	.loc 2 460 0
	add	r7, r8, r5, lsl #3
.LVL153:
	.loc 2 462 0
	cmp	r7, r3
	beq	.L161
	.loc 2 465 0
	ldr	r2, [r4, #2300]
	lsl	r3, r5, #1
	.loc 2 466 0
	ldrh	r6, [r7, #4]
	.loc 2 465 0
	ldrh	r3, [r2, r3]
.LVL154:
	.loc 2 467 0
	cmp	r6, #0
	.loc 2 468 0
	mvneq	r6, #0
.LVL155:
	.loc 2 466 0
	mulne	r6, r6, r3
.LVL156:
	.loc 2 470 0
	ldr	r3, [r8, r5, lsl #3]
.LVL157:
	cmn	r3, #1
	bne	.L164
	movw	r2, #470
	ldr	r1, .L172+4
	ldr	r0, .L172+8
	bl	printk
.LVL158:
.L164:
	.loc 2 471 0
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L161
	.loc 2 473 0
	ldrh	r2, [r7, #2]
	.loc 2 475 0
	ldr	r1, [r4, #2300]
	.loc 2 473 0
	lsl	r2, r2, #3
	.loc 2 475 0
	lsr	r3, r2, #2
	ldrh	r0, [r1, r3]
.LVL159:
	.loc 2 476 0
	ldr	r1, [r4, #2292]
	add	r2, r1, r2
	ldrh	r3, [r2, #4]
	.loc 2 477 0
	cmp	r3, #0
	.loc 2 476 0
	mulne	r3, r3, r0
.LVL160:
	.loc 2 478 0
	mvneq	r3, #0
	.loc 2 480 0
	cmp	r6, r3
	bcs	.L161
	.loc 2 481 0
	mov	r1, r5
	ldr	r0, .L172+12
.LVL161:
	bl	List_remove_node
.LVL162:
	movw	r3, #2312
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L166
	movw	r2, #481
	ldr	r1, .L172+4
	ldr	r0, .L172+8
	bl	printk
.LVL163:
.L166:
	movw	r2, #2312
	.loc 2 482 0
	mov	r0, r5
	.loc 2 481 0
	ldrh	r3, [r4, r2]
	sub	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
	.loc 2 482 0
	bl	INSERT_DATA_LIST
.LVL164:
.L161:
.LBE274:
.LBE273:
	.loc 2 485 0
	mov	r0, #0
.LVL165:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L173:
	.align	2
.L172:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC1
	.word	.LANCHOR0+2296
.LFE202:
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
.LFB210:
	.loc 2 749 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI95:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI96:
	sub	fp, ip, #4
.LCFI97:
	.loc 2 754 0
	ldr	r0, .L184
	mov	r1, #0
	.loc 2 755 0
	movw	ip, #65535
	.loc 2 754 0
	ldrh	r2, [r0, #114]
	mov	lr, r0
	.loc 2 755 0
	ldr	r3, [r0, #2468]
	mov	r0, #12
.L175:
	uxth	r4, r1
	.loc 2 754 0 discriminator 1
	cmp	r4, r2
	bcc	.L177
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	.loc 2 765 0
	mov	r6, #12
.L178:
	uxth	r5, r1
	.loc 2 764 0 discriminator 1
	cmp	r5, r2
	bcc	.L180
	.loc 2 773 0
	cmp	r4, r2
	bcc	.L176
	.loc 2 779 0
	movw	r1, #2472
	mov	r4, r2
	ldrh	r5, [lr, r1]
	mvn	r0, #0
	mov	r1, #0
.L181:
	uxth	ip, r1
	.loc 2 778 0 discriminator 1
	cmp	ip, r2
	bcc	.L183
	.loc 2 784 0
	cmp	r4, r2
	bcc	.L176
	.loc 2 784 0 is_stmt 0 discriminator 1
	mov	r2, #784
	ldr	r1, .L184+4
	ldr	r0, .L184+8
	bl	printk
.LVL166:
	b	.L176
.L177:
	add	r1, r1, #1
	.loc 2 755 0 is_stmt 1
	mla	r5, r0, r1, r3
	ldrh	r5, [r5, #-12]
	cmp	r5, ip
	bne	.L175
.L176:
	.loc 2 786 0
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L180:
	.loc 2 765 0
	mla	ip, r6, r1, r3
	add	r1, r1, #1
	ldr	ip, [ip, #4]
	.loc 2 766 0
	cmp	r0, ip
	movls	r7, #0
	movhi	r7, #1
	cmp	ip, #0
	movlt	r7, #0
	cmp	r7, #0
	movne	r0, ip
	movne	r4, r5
	b	.L178
.L183:
	.loc 2 779 0
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L182
	.loc 2 779 0 is_stmt 0 discriminator 1
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L182:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L181
.L185:
	.align	2
.L184:
	.word	.LANCHOR0
	.word	.LANCHOR1+73
	.word	.LC1
.LFE210:
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
.LFB216:
	.loc 2 931 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL167:
	mov	ip, sp
.LCFI98:
	push	{fp, ip, lr, pc}
.LCFI99:
	sub	fp, ip, #4
.LCFI100:
	.loc 2 934 0
	ldr	ip, .L192
	movw	r1, #2474
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L187
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	.loc 2 943 0
	ldmfd	sp, {fp, sp, pc}
.L187:
	.loc 2 935 0
	mov	r2, #0
.LBB277:
.LBB278:
	.loc 2 939 0
	movw	lr, #65535
.LBE278:
.LBE277:
	.loc 2 935 0
	strh	r2, [ip, r1]	@ movhi
	.loc 2 936 0
	str	r2, [ip, #2476]
	ldrh	r1, [ip, #16]
.LBB280:
.LBB279:
	.loc 2 939 0
	ldr	r2, [ip, #2300]
	add	r1, r2, r1, lsl #1
.L188:
	.loc 2 937 0
	cmp	r2, r1
	bne	.L190
	ldmfd	sp, {fp, sp, pc}
.L190:
	.loc 2 939 0
	ldrh	ip, [r2], #2
	cmp	ip, lr
	.loc 2 940 0
	ldrne	r0, [r3, #2476]
.LVL168:
	addne	r0, r0, ip
	strne	r0, [r3, #2476]
	b	.L188
.L193:
	.align	2
.L192:
	.word	.LANCHOR0
.LBE279:
.LBE280:
.LFE216:
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
.LFB222:
	.loc 2 1396 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL169:
	mov	ip, sp
.LCFI101:
	push	{fp, ip, lr, pc}
.LCFI102:
	sub	fp, ip, #4
.LCFI103:
	.loc 2 1397 0
	cmp	r1, #0
	.loc 2 1396 0
	uxth	r0, r0
	.loc 2 1397 0
	beq	.L195
	bl	ftl_set_blk_mode.part.6
.LVL170:
	ldmfd	sp, {fp, sp, pc}
.LVL171:
.L195:
	.loc 2 1400 0
	ldr	r3, .L197
	lsr	r1, r0, #5
.LVL172:
	mov	ip, #1
	and	r0, r0, #31
.LVL173:
	ldr	r2, [r3]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L198:
	.align	2
.L197:
	.word	.LANCHOR0
.LFE222:
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
.LFB223:
	.loc 2 1404 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL174:
	mov	ip, sp
.LCFI104:
	push	{fp, ip, lr, pc}
.LCFI105:
	sub	fp, ip, #4
.LCFI106:
	.loc 2 1405 0
	ldr	r3, .L200
	.loc 2 1404 0
	uxth	r0, r0
	.loc 2 1405 0
	ldr	r3, [r3]
	lsr	r2, r0, #5
	and	r0, r0, #31
.LVL175:
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	.loc 2 1406 0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L201:
	.align	2
.L200:
	.word	.LANCHOR0
.LFE223:
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
.LFB229:
	.loc 2 1657 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL176:
	mov	ip, sp
.LCFI107:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI108:
	sub	fp, ip, #4
.LCFI109:
	.loc 2 1659 0
	mov	r3, #0
	.loc 2 1657 0
	uxth	r2, r2
	uxth	r5, r1
.LVL177:
	.loc 2 1659 0
	strh	r3, [r0, #4]	@ movhi
	.loc 2 1660 0
	ldr	r3, .L209
	add	ip, r0, r2, lsl #1
	.loc 2 1662 0
	movw	r1, #65535
	add	ip, ip, #14
	.loc 2 1660 0
	ldrh	lr, [r3, #8]
.L203:
	.loc 2 1660 0 is_stmt 0 discriminator 1
	cmp	r2, lr
	bcc	.L205
	ldrh	r1, [r3, #78]
	add	ip, r0, #16
	.loc 2 1671 0 is_stmt 1
	mov	r3, #0
	.loc 2 1669 0
	movw	r4, #65535
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L206:
	.loc 2 1667 0 discriminator 1
	uxth	r2, r3
.LVL178:
	cmp	lr, r2
	bhi	.L208
	.loc 2 1674 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.LVL179:
.L205:
	.loc 2 1662 0
	ldrh	r4, [ip, #2]!
	.loc 2 1660 0
	add	r2, r2, #1
.LVL180:
	uxth	r2, r2
.LVL181:
	.loc 2 1662 0
	cmp	r4, r1
	.loc 2 1664 0
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L203
.LVL182:
.L208:
	.loc 2 1669 0
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	.loc 2 1671 0
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L206
.L210:
	.align	2
.L209:
	.word	.LANCHOR0
.LFE229:
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
.LFB232:
	.loc 2 2062 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL183:
	mov	ip, sp
.LCFI110:
	push	{fp, ip, lr, pc}
.LCFI111:
	sub	fp, ip, #4
.LCFI112:
	.loc 2 2065 0
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	ldmfdeq	sp, {fp, sp, pc}
	.loc 2 2067 0
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	ldmfdeq	sp, {fp, sp, pc}
.LBB283:
.LBB284:
	.loc 2 2069 0
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	.loc 2 2074 0
	mov	lr, #0
	.loc 2 2069 0
	add	r2, r0, r2, lsl #1
	ldrh	r1, [r2, #16]
.LVL184:
	.loc 2 2072 0
	ldr	r2, .L217
	ldrh	ip, [r2, #8]
	.loc 2 2070 0
	mov	r2, r3
.L214:
	cmp	r1, r2
	beq	.L216
	ldmfd	sp, {fp, sp, pc}
.L216:
	.loc 2 2071 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2072 0
	cmp	r3, ip
	.loc 2 2071 0
	strb	r3, [r0, #6]
	.loc 2 2073 0
	ldrheq	r3, [r0, #2]
	.loc 2 2074 0
	strbeq	lr, [r0, #6]
	.loc 2 2073 0
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	.loc 2 2076 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r0, r3, lsl #1
	ldrh	r1, [r3, #16]
	b	.L214
.L218:
	.align	2
.L217:
	.word	.LANCHOR0
.LBE284:
.LBE283:
.LFE232:
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
.LFB234:
	.loc 2 2094 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL185:
	mov	ip, sp
.LCFI113:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI114:
	sub	fp, ip, #4
.LCFI115:
	sub	sp, sp, #4
	.loc 2 2094 0
	mov	r4, r0
	.loc 2 2097 0
	ldr	r6, .L224
	ldrh	r2, [r0]
	ldrh	r3, [r6, #16]
	cmp	r2, r3
	bcc	.L220
	.loc 2 2097 0 is_stmt 0 discriminator 1
	movw	r2, #2097
	ldr	r1, .L224+4
	ldr	r0, .L224+8
	bl	printk
.LVL186:
.L220:
	.loc 2 2100 0 is_stmt 1
	ldrh	r9, [r6, #8]
	add	r7, r4, #16
	.loc 2 2101 0
	ldr	r10, .L224+12
	.loc 2 2102 0
	mvn	r8, #0
	.loc 2 2098 0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	.loc 2 2099 0
	strb	r5, [r4, #7]
.L221:
	.loc 2 2100 0 discriminator 1
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L223
	.loc 2 2110 0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	.loc 2 2113 0
	mov	r0, #0
	.loc 2 2110 0
	ldrh	r2, [r6, #78]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2111 0
	mov	r3, #1
	strb	r3, [r4, #9]
	.loc 2 2113 0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L223:
	.loc 2 2101 0
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
.LVL187:
	.loc 2 2102 0
	strh	r8, [r7, #-2]	@ movhi
	.loc 2 2101 0
	str	r0, [fp, #-44]
	.loc 2 2103 0
	bl	FtlBbmIsBadBlock
.LVL188:
	cmp	r0, #0
	.loc 2 2104 0
	ldreq	r3, [fp, #-44]
	strheq	r3, [r7, #-2]	@ movhi
	.loc 2 2105 0
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L221
.L225:
	.align	2
.L224:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC1
	.word	.LANCHOR0+36
.LFE234:
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
.LFB243:
	.loc 2 2451 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL189:
	mov	ip, sp
.LCFI116:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI117:
	sub	fp, ip, #4
.LCFI118:
	.loc 2 2455 0
	ldr	r3, .L232
	mov	r5, #0
	.loc 2 2451 0
	uxth	r6, r0
.LVL190:
	.loc 2 2453 0
	mov	r4, r5
	.loc 2 2455 0
	ldrh	r8, [r3, #8]
	mov	r7, r3
	.loc 2 2458 0
	ldrh	r9, [r3, #78]
	.loc 2 2456 0
	add	r10, r3, #36
.L227:
	.loc 2 2455 0 discriminator 1
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L229
	.loc 2 2460 0
	cmp	r4, #0
	moveq	r0, r4
	beq	.L230
	.loc 2 2461 0
	mov	r1, r4
	mov	r0, #32768
	bl	__divsi3
.LVL191:
.L230:
	.loc 2 2462 0
	ldr	r1, [r7, #2292]
	add	r1, r1, r6, lsl #3
	strh	r0, [r1, #4]	@ movhi
	.loc 2 2464 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L229:
	.loc 2 2456 0
	mov	r1, r6
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL192:
	.loc 2 2457 0
	bl	FtlBbmIsBadBlock
.LVL193:
	cmp	r0, #0
	add	r5, r5, #1
	.loc 2 2458 0
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L227
.L233:
	.align	2
.L232:
	.word	.LANCHOR0
.LFE243:
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
.LFB244:
	.loc 2 2467 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI119:
	push	{fp, ip, lr, pc}
.LCFI120:
	sub	fp, ip, #4
.LCFI121:
	.loc 2 2470 0
	ldr	r3, .L237
	ldr	r0, [r3, #2316]
	cmp	r0, #0
	.loc 2 2471 0
	ldrne	r2, [r3, #2292]
	ldrne	r3, [r3, #2304]
	subne	r0, r0, r2
	ubfxne	r0, r0, #3, #16
	lslne	r0, r0, #1
	ldrhne	r0, [r3, r0]
	.loc 2 2473 0
	ldmfd	sp, {fp, sp, pc}
.L238:
	.align	2
.L237:
	.word	.LANCHOR0
.LFE244:
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
.LFB245:
	.loc 2 2476 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL194:
	mov	ip, sp
.LCFI122:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI123:
	sub	fp, ip, #4
.LCFI124:
	.loc 2 2481 0
	ldr	r2, .L247
	.loc 2 2476 0
	uxth	r4, r0
.LVL195:
	.loc 2 2481 0
	ldr	r0, [r2, #2316]
	cmp	r0, #0
	ldmfdeq	sp, {r4, r5, fp, sp, pc}
	.loc 2 2482 0
	add	r3, r2, #2320
	.loc 2 2484 0
	ldr	ip, [r2, #2292]
	.loc 2 2482 0
	ldrh	r3, [r3]
	.loc 2 2485 0
	mov	r1, #0
	.loc 2 2486 0
	movw	r5, #65535
	.loc 2 2482 0
	rsb	r3, r3, r3, lsl #3
	asr	r3, r3, #3
	cmp	r4, r3
	.loc 2 2483 0
	uxthgt	r4, r3
	.loc 2 2484 0
	sub	r3, r0, ip
	ubfx	r3, r3, #3, #16
.L242:
	.loc 2 2485 0 discriminator 1
	uxth	r0, r1
	cmp	r4, r0
	bls	.L244
	.loc 2 2486 0
	lsl	r0, r3, #3
	add	r1, r1, #1
	ldrh	lr, [ip, r0]
	cmp	lr, r5
	bne	.L246
.L244:
	.loc 2 2490 0
	ldr	r2, [r2, #2304]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L246:
	mov	r3, lr
	b	.L242
.L248:
	.align	2
.L247:
	.word	.LANCHOR0
.LFE245:
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
.LFB150:
	.loc 3 11 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL196:
	mov	ip, sp
.LCFI125:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI126:
	sub	fp, ip, #4
.LCFI127:
	sub	sp, sp, #12
	.loc 3 11 0
	mov	r7, r0
	.loc 3 16 0
	ldr	r6, .L261
	.loc 3 15 0
	add	r5, r7, #12
	ldr	r1, .L261+4
	.loc 3 49 0
	movw	r4, #2324
	.loc 3 15 0
	bl	strcpy
.LVL197:
	.loc 3 16 0
	ldr	r2, [r6, #92]
	mov	r0, r5
	ldr	r1, .L261+8
	.loc 3 31 0
	add	r9, r6, #2320
	.loc 3 16 0
	bl	sprintf
.LVL198:
	add	r5, r5, r0
	.loc 3 17 0
	ldr	r1, .L261+12
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
.LVL199:
	.loc 3 18 0
	ldr	r2, [r6, #2480]
	mov	r0, r5
	ldr	r1, .L261+16
	.loc 3 66 0
	movw	r8, #2564
	.loc 3 18 0
	bl	sprintf
.LVL200:
	add	r5, r5, r0
	.loc 3 19 0
	ldr	r2, [r6, #2476]
	ldr	r1, .L261+20
	mov	r0, r5
	bl	sprintf
.LVL201:
	add	r5, r5, r0
	.loc 3 20 0
	ldr	r2, [r6, #2484]
	ldr	r1, .L261+24
	mov	r0, r5
	bl	sprintf
.LVL202:
	add	r5, r5, r0
	.loc 3 21 0
	ldr	r2, [r6, #2488]
	ldr	r1, .L261+28
	mov	r0, r5
	bl	sprintf
.LVL203:
	add	r5, r5, r0
	.loc 3 22 0
	ldr	r2, [r6, #2492]
	ldr	r1, .L261+32
	mov	r0, r5
	bl	sprintf
.LVL204:
	add	r5, r5, r0
	.loc 3 23 0
	ldr	r2, [r6, #2496]
	ldr	r1, .L261+36
	mov	r0, r5
	bl	sprintf
.LVL205:
	add	r5, r5, r0
	.loc 3 24 0
	ldr	r2, [r6, #2500]
	ldr	r1, .L261+40
	mov	r0, r5
	bl	sprintf
.LVL206:
	add	r5, r5, r0
	.loc 3 25 0
	ldr	r2, [r6, #2504]
	ldr	r1, .L261+44
	mov	r0, r5
	bl	sprintf
.LVL207:
	.loc 3 26 0
	ldr	r2, [r6, #2508]
	.loc 3 25 0
	add	r5, r5, r0
	.loc 3 26 0
	ldr	r1, .L261+48
	mov	r0, r5
	lsr	r2, r2, #11
	bl	sprintf
.LVL208:
	.loc 3 27 0
	ldr	r2, [r6, #2512]
	.loc 3 26 0
	add	r5, r5, r0
	.loc 3 27 0
	ldr	r1, .L261+52
	mov	r0, r5
	lsr	r2, r2, #11
	bl	sprintf
.LVL209:
	add	r5, r5, r0
	.loc 3 28 0
	ldr	r2, [r6, #2516]
	ldr	r1, .L261+56
	mov	r0, r5
	bl	sprintf
.LVL210:
	add	r5, r5, r0
	.loc 3 29 0
	ldr	r2, [r6, #2520]
	ldr	r1, .L261+60
	mov	r0, r5
	bl	sprintf
.LVL211:
	add	r5, r5, r0
	.loc 3 30 0
	ldrh	r2, [r6, #134]
	ldr	r1, .L261+64
	mov	r0, r5
	bl	sprintf
.LVL212:
	add	r5, r5, r0
	.loc 3 31 0
	ldrh	r2, [r9]
	ldr	r1, .L261+68
	mov	r0, r5
	bl	sprintf
.LVL213:
	add	r5, r5, r0
	.loc 3 32 0
	ldr	r2, [r6, #2524]
	ldr	r1, .L261+72
	mov	r0, r5
	bl	sprintf
.LVL214:
	add	r5, r5, r0
	.loc 3 33 0
	ldr	r2, [r6, #2528]
	ldr	r1, .L261+76
	mov	r0, r5
	bl	sprintf
.LVL215:
	add	r5, r5, r0
	.loc 3 34 0
	ldr	r2, [r6, #2532]
	ldr	r1, .L261+80
	mov	r0, r5
	bl	sprintf
.LVL216:
	add	r5, r5, r0
	.loc 3 35 0
	ldr	r2, [r6, #2536]
	ldr	r1, .L261+84
	mov	r0, r5
	bl	sprintf
.LVL217:
	add	r5, r5, r0
	.loc 3 36 0
	ldr	r2, [r6, #2540]
	ldr	r1, .L261+88
	mov	r0, r5
	bl	sprintf
.LVL218:
	add	r5, r5, r0
	.loc 3 37 0
	ldr	r2, [r6, #2544]
	ldr	r1, .L261+92
	mov	r0, r5
	bl	sprintf
.LVL219:
	add	r5, r5, r0
	.loc 3 38 0
	ldrh	r2, [r9, #-46]
	ldr	r1, .L261+96
	mov	r0, r5
	bl	sprintf
.LVL220:
	add	r5, r5, r0
	.loc 3 39 0
	ldrh	r2, [r9, #-48]
	ldr	r1, .L261+100
	mov	r0, r5
	bl	sprintf
.LVL221:
	add	r5, r5, r0
	.loc 3 40 0
	ldr	r2, [r6, #116]
	ldr	r1, .L261+104
	mov	r0, r5
	bl	sprintf
.LVL222:
	add	r5, r5, r0
	.loc 3 41 0
	ldr	r2, [r6, #108]
	ldr	r1, .L261+108
	mov	r0, r5
	bl	sprintf
.LVL223:
	add	r5, r5, r0
	.loc 3 42 0
	ldr	r2, [r6, #4]
	ldr	r1, .L261+112
	mov	r0, r5
	bl	sprintf
.LVL224:
	add	r5, r5, r0
	.loc 3 43 0
	ldrh	r2, [r6, #194]
	ldr	r1, .L261+116
	mov	r0, r5
	bl	sprintf
.LVL225:
	add	r5, r5, r0
	.loc 3 44 0
	ldrh	r2, [r6, #16]
	ldr	r1, .L261+120
	mov	r0, r5
	bl	sprintf
.LVL226:
	.loc 3 45 0
	movw	r3, #2548
	.loc 3 44 0
	add	r5, r5, r0
	.loc 3 45 0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L261+124
	bl	sprintf
.LVL227:
	add	r5, r5, r0
	.loc 3 46 0
	ldr	r2, [r6, #20]
	ldr	r1, .L261+128
	mov	r0, r5
	bl	sprintf
.LVL228:
	.loc 3 47 0
	movw	r3, #2552
	.loc 3 46 0
	add	r5, r5, r0
	.loc 3 47 0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L261+132
	bl	sprintf
.LVL229:
	add	r5, r5, r0
	.loc 3 48 0
	ldrh	r2, [r6, #128]
	ldr	r1, .L261+136
	mov	r0, r5
	bl	sprintf
.LVL230:
	add	r5, r5, r0
	.loc 3 49 0
	ldrh	r2, [r9, #6]
	ldr	r1, .L261+140
	mov	r0, r5
	bl	sprintf
.LVL231:
	add	r5, r5, r0
	.loc 3 50 0
	ldrb	r2, [r6, #2330]	@ zero_extendqisi2
	ldr	r1, .L261+144
	mov	r0, r5
	bl	sprintf
.LVL232:
	add	r5, r5, r0
	.loc 3 51 0
	ldrh	r2, [r6, r4]
	ldr	r1, .L261+148
	mov	r0, r5
	bl	sprintf
.LVL233:
	add	r5, r5, r0
	.loc 3 52 0
	ldrb	r2, [r6, #2332]	@ zero_extendqisi2
	ldr	r1, .L261+152
	mov	r0, r5
	bl	sprintf
.LVL234:
	add	r5, r5, r0
	.loc 3 53 0
	ldrh	r2, [r9, #8]
	ldr	r1, .L261+156
	mov	r0, r5
	bl	sprintf
.LVL235:
	.loc 3 54 0
	ldrh	r3, [r6, r4]
	.loc 3 53 0
	add	r5, r5, r0
	.loc 3 54 0
	ldr	r2, [r6, #2300]
	mov	r0, r5
	ldr	r1, .L261+160
	.loc 3 55 0
	movw	r4, #2372
	.loc 3 54 0
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
.LVL236:
	add	r5, r5, r0
	.loc 3 55 0
	ldrh	r2, [r9, #54]
	ldr	r1, .L261+164
	mov	r0, r5
	bl	sprintf
.LVL237:
	add	r5, r5, r0
	.loc 3 56 0
	ldrb	r2, [r6, #2378]	@ zero_extendqisi2
	ldr	r1, .L261+168
	mov	r0, r5
	bl	sprintf
.LVL238:
	add	r5, r5, r0
	.loc 3 57 0
	ldrh	r2, [r6, r4]
	ldr	r1, .L261+172
	mov	r0, r5
	bl	sprintf
.LVL239:
	add	r5, r5, r0
	.loc 3 58 0
	ldrb	r2, [r6, #2380]	@ zero_extendqisi2
	ldr	r1, .L261+176
	mov	r0, r5
	bl	sprintf
.LVL240:
	add	r5, r5, r0
	.loc 3 59 0
	ldrh	r2, [r9, #56]
	ldr	r1, .L261+180
	mov	r0, r5
	bl	sprintf
.LVL241:
	.loc 3 60 0
	ldrh	r3, [r6, r4]
	.loc 3 59 0
	add	r5, r5, r0
	.loc 3 60 0
	ldr	r2, [r6, #2300]
	mov	r0, r5
	ldr	r1, .L261+184
	.loc 3 61 0
	add	r4, r9, #100
	.loc 3 60 0
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
.LVL242:
	add	r5, r5, r0
	.loc 3 61 0
	ldrh	r2, [r9, #102]
	ldr	r1, .L261+188
	mov	r0, r5
	bl	sprintf
.LVL243:
	add	r5, r5, r0
	.loc 3 62 0
	ldrb	r2, [r6, #2426]	@ zero_extendqisi2
	ldr	r1, .L261+192
	mov	r0, r5
	bl	sprintf
.LVL244:
	.loc 3 63 0
	movw	r3, #2420
	.loc 3 62 0
	add	r5, r5, r0
	.loc 3 63 0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L261+196
	bl	sprintf
.LVL245:
	add	r5, r5, r0
	.loc 3 64 0
	ldrb	r2, [r6, #2428]	@ zero_extendqisi2
	ldr	r1, .L261+200
	mov	r0, r5
	bl	sprintf
.LVL246:
	add	r5, r5, r0
	.loc 3 65 0
	ldrh	r2, [r9, #104]
	ldr	r1, .L261+204
	mov	r0, r5
	bl	sprintf
.LVL247:
	add	r5, r5, r0
	.loc 3 66 0
	ldrh	r2, [r4, #146]
	ldr	r1, .L261+208
	mov	r0, r5
	bl	sprintf
.LVL248:
	add	r5, r5, r0
	.loc 3 67 0
	ldrb	r2, [r6, #2570]	@ zero_extendqisi2
	ldr	r1, .L261+212
	mov	r0, r5
	bl	sprintf
.LVL249:
	add	r5, r5, r0
	.loc 3 68 0
	ldrh	r2, [r6, r8]
	ldr	r1, .L261+216
	mov	r0, r5
	bl	sprintf
.LVL250:
	add	r5, r5, r0
	.loc 3 69 0
	ldrb	r2, [r6, #2572]	@ zero_extendqisi2
	ldr	r1, .L261+220
	mov	r0, r5
	bl	sprintf
.LVL251:
	add	r5, r5, r0
	.loc 3 70 0
	ldrh	r2, [r4, #148]
	ldr	r1, .L261+224
	mov	r0, r5
	bl	sprintf
.LVL252:
	.loc 3 71 0
	ldr	r3, [r6, #2688]
	.loc 3 70 0
	add	r5, r5, r0
	.loc 3 71 0
	ldr	r2, [r6, #2692]
	mov	r0, r5
	ldr	r1, .L261+228
	str	r3, [sp]
	ldr	r3, [r6, #2696]
	bl	sprintf
.LVL253:
	add	r4, r5, r0
	.loc 3 72 0
	ldr	r2, [r6, #2684]
	ldr	r1, .L261+232
	mov	r0, r4
	bl	sprintf
.LVL254:
	add	r4, r4, r0
	.loc 3 73 0
	ldr	r2, [r6, #2708]
	ldr	r1, .L261+236
	mov	r0, r4
	bl	sprintf
.LVL255:
	.loc 3 74 0
	movw	r3, #3124
	.loc 3 73 0
	add	r4, r4, r0
	.loc 3 74 0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L261+240
	bl	sprintf
.LVL256:
	.loc 3 75 0
	movw	r3, #3126
	.loc 3 74 0
	add	r4, r4, r0
	.loc 3 75 0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L261+244
	bl	sprintf
.LVL257:
	add	r4, r4, r0
	.loc 3 76 0
	ldr	r2, [r6, #3128]
	ldr	r1, .L261+248
	mov	r0, r4
	bl	sprintf
.LVL258:
	.loc 3 77 0
	movw	r3, #3132
	.loc 3 76 0
	add	r4, r4, r0
	.loc 3 77 0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L261+252
	bl	sprintf
.LVL259:
	add	r4, r4, r0
	.loc 3 78 0
	bl	GetFreeBlockMinEraseCount
.LVL260:
	ldr	r1, .L261+256
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL261:
	add	r4, r4, r0
	.loc 3 79 0
	ldrh	r0, [r9]
	bl	GetFreeBlockMaxEraseCount
.LVL262:
	ldr	r1, .L261+260
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL263:
	.loc 3 80 0
	ldr	r3, .L261+264
	.loc 3 79 0
	add	r4, r4, r0
	.loc 3 80 0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L250
.LVL264:
.L255:
	.loc 3 103 0
	sub	r0, r4, r7
	.loc 3 104 0
	sub	sp, fp, #40
.LCFI128:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL265:
.L250:
.LCFI129:
	.loc 3 83 0
	ldrh	r3, [r6, r8]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L252
	.loc 3 84 0
	ldr	r2, [r6, #2300]
	lsl	r3, r3, #1
	mov	r0, r4
	ldr	r1, .L261+268
	ldrh	r2, [r2, r3]
	bl	sprintf
.LVL266:
	add	r4, r4, r0
.L252:
.LBB285:
	.loc 3 86 0
	mov	r0, #0
	.loc 3 90 0
	ldr	r9, .L261+272
	.loc 3 86 0
	bl	List_get_gc_head_node
.LVL267:
	uxth	r3, r0
.LVL268:
	.loc 3 87 0
	mov	r5, #0
	.loc 3 88 0
	movw	r10, #65535
.L254:
	cmp	r3, r10
	beq	.L253
	.loc 3 90 0 discriminator 2
	ldr	r2, [r6, #2304]
	lsl	r1, r3, #1
	lsl	r8, r3, #3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp, #8]
	ldr	r2, [r6, #2292]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2300]
	ldrh	r2, [r2, r1]
	mov	r1, r9
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
.LVL269:
	.loc 3 87 0 discriminator 2
	add	r5, r5, #1
	.loc 3 91 0 discriminator 2
	ldr	r3, [r6, #2292]
	.loc 3 87 0 discriminator 2
	cmp	r5, #16
	.loc 3 90 0 discriminator 2
	add	r4, r4, r0
	.loc 3 91 0 discriminator 2
	ldrh	r3, [r3, r8]
.LVL270:
	.loc 3 87 0 discriminator 2
	bne	.L254
.L253:
.LBE285:
.LBB286:
	.loc 3 95 0
	ldr	r3, [r6, #2316]
.LVL271:
	.loc 3 96 0
	mov	r5, #0
	.loc 3 95 0
	ldr	r2, [r6, #2292]
	.loc 3 97 0
	movw	r9, #65535
	.loc 3 99 0
	ldr	r10, .L261+276
	.loc 3 95 0
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.LVL272:
.L256:
	.loc 3 97 0
	cmp	r3, r9
	beq	.L255
	.loc 3 99 0 discriminator 2
	ldr	r1, [r6, #2304]
	lsl	r2, r3, #1
	lsl	r8, r3, #3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, r10
	str	r2, [sp, #4]
	ldr	r2, [r6, #2292]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	.loc 3 96 0 discriminator 2
	add	r5, r5, #1
	.loc 3 99 0 discriminator 2
	bl	sprintf
.LVL273:
	.loc 3 96 0 discriminator 2
	cmp	r5, #4
	.loc 3 99 0 discriminator 2
	add	r4, r4, r0
	.loc 3 96 0 discriminator 2
	beq	.L255
	.loc 3 100 0
	ldr	r3, [r6, #2292]
	ldrh	r3, [r3, r8]
.LVL274:
	b	.L256
.L262:
	.align	2
.L261:
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
	.word	.LC63
	.word	.LC64
	.word	.LC65
	.word	.LC66
	.word	.LANCHOR2
	.word	.LC67
	.word	.LC68
	.word	.LC69
.LBE286:
.LFE150:
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
.LFB151:
	.loc 3 113 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL275:
	mov	ip, sp
.LCFI130:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI131:
	sub	fp, ip, #4
.LCFI132:
	.loc 3 119 0
	ldr	r2, .L264
	.loc 3 113 0
	mov	r5, r0
	.loc 3 119 0
	ldr	r1, .L264+4
	bl	sprintf
.LVL276:
	add	r4, r5, r0
	.loc 3 121 0
	mov	r0, r4
	bl	FtlPrintInfo2buf
.LVL277:
	add	r0, r4, r0
	.loc 3 123 0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L265:
	.align	2
.L264:
	.word	.LC70
	.word	.LC71
.LFE151:
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
.LFB246:
	.loc 2 2497 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI133:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI134:
	sub	fp, ip, #4
.LCFI135:
	sub	sp, sp, #16
	.loc 2 2507 0
	ldr	r4, .L290
	ldr	r2, [r4, #2532]
	ldr	r3, [r4, #2544]
	cmp	r2, r3
	bcs	.L267
	ldr	r2, [r4, #2304]
	.loc 2 2509 0
	mov	r3, #0
	.loc 2 2510 0
	ldrh	r1, [r4, #16]
	.loc 2 2509 0
	str	r3, [r4, #2524]
	sub	r2, r2, #2
.L268:
	.loc 2 2510 0 discriminator 1
	cmp	r3, r1
	bcc	.L269
	.loc 2 2512 0
	ldr	r5, [r4, #2524]
	mov	r0, r5
	bl	__udivsi3
.LVL278:
	str	r0, [r4, #2532]
	.loc 2 2513 0
	ldr	r0, [r4, #2528]
	ldrh	r1, [r4, #68]
	sub	r0, r5, r0
	bl	__udivsi3
.LVL279:
	str	r0, [r4, #2524]
.L270:
	.loc 2 2519 0
	ldr	r6, [r4, #2544]
	ldr	r7, [r4, #2532]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L275
	.loc 2 2519 0 is_stmt 0 discriminator 1
	ldr	r2, [r4, #2540]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L275
.LVL280:
.L277:
	.loc 2 2520 0 is_stmt 1
	movw	r5, #65535
.L276:
	.loc 2 2566 0
	mov	r0, r5
.LVL281:
	sub	sp, fp, #40
.LCFI136:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL282:
.L269:
.LCFI137:
	.loc 2 2511 0 discriminator 3
	ldrh	r0, [r2, #2]!
	.loc 2 2510 0 discriminator 3
	add	r3, r3, #1
	.loc 2 2511 0 discriminator 3
	ldr	ip, [r4, #2524]
	add	r0, r0, ip
	str	r0, [r4, #2524]
	b	.L268
.L267:
	.loc 2 2514 0
	ldr	r3, [r4, #2540]
	cmp	r2, r3
	.loc 2 2515 0
	addhi	r3, r3, #1
	strhi	r3, [r4, #2540]
	.loc 2 2516 0
	movhi	r3, #0
	.loc 2 2514 0
	bls	.L270
.L272:
	.loc 2 2516 0 discriminator 1
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	bcs	.L270
	.loc 2 2517 0 discriminator 3
	ldr	r0, [r4, #2304]
	lsl	r1, r3, #1
	.loc 2 2516 0 discriminator 3
	add	r3, r3, #1
	.loc 2 2517 0 discriminator 3
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L272
.L275:
	.loc 2 2522 0
	ldr	r3, .L290+4
	ldrh	r0, [r3]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
.LVL283:
	.loc 2 2523 0
	add	r3, r6, #64
	.loc 2 2522 0
	mov	r9, r0
.LVL284:
	.loc 2 2523 0
	cmp	r0, r3
	bcc	.L277
	.loc 2 2526 0
	ldr	r3, [r4, #2296]
.LVL285:
	cmp	r3, #0
	beq	.L277
	.loc 2 2530 0
	ldrh	r10, [r4, #16]
	.loc 2 2534 0
	movw	r1, #65535
.LVL286:
	.loc 2 2533 0
	ldr	lr, [r4, #2292]
	.loc 2 2534 0
	mov	r0, #0
.LVL287:
	ldr	r8, [r4, #2304]
	mov	r2, r1
.L278:
	.loc 2 2528 0
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L281
	mov	r5, r2
.L280:
	.loc 2 2546 0
	movw	r3, #65535
.LVL288:
	cmp	r5, r3
	beq	.L277
	.loc 2 2548 0
	lsl	r3, r5, #1
	ldrh	r10, [r8, r3]
.LVL289:
	.loc 2 2549 0
	cmp	r6, r10
	bcs	.L282
	str	r3, [fp, #-44]
	.loc 2 2551 0
	bl	GetFreeBlockMinEraseCount
.LVL290:
	ldr	r3, [fp, #-44]
	cmp	r6, r0
	.loc 2 2552 0
	strcc	r1, [r4, #2544]
.LVL291:
.L282:
	.loc 2 2554 0
	cmp	r7, r10
	bls	.L277
	.loc 2 2554 0 is_stmt 0 discriminator 1
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L277
	.loc 2 2556 0 is_stmt 1
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L283
	.loc 2 2556 0 is_stmt 0 discriminator 1
	ldr	r2, [r4, #2540]
	add	r10, r10, #768
.LVL292:
	cmp	r10, r2
	bcs	.L277
.LVL293:
.L283:
	.loc 2 2558 0 is_stmt 1
	str	r9, [sp, #8]
	mov	r1, r5
.LVL294:
	ldrh	r2, [r8, r3]
	ldr	r0, .L290+8
.LVL295:
	str	r2, [sp, #4]
	ldr	r2, [r4, #2300]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2540]
	bl	printk
.LVL296:
	.loc 2 2559 0
	mov	r3, #1
	str	r3, [r4, #3136]
	.loc 2 2560 0
	b	.L276
.LVL297:
.L281:
	.loc 2 2529 0
	add	r0, r0, #1
.LVL298:
	uxth	r0, r0
.LVL299:
	.loc 2 2530 0
	cmp	r0, r10
	bhi	.L277
	.loc 2 2532 0
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L279
	.loc 2 2533 0
	sub	r3, r3, lr
.LVL300:
	asr	r3, r3, #3
	uxth	r5, r3
	.loc 2 2534 0
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L280
	.loc 2 2533 0
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.LVL301:
.L279:
	.loc 2 2543 0
	add	r3, lr, ip, lsl #3
	b	.L278
.L291:
	.align	2
.L290:
	.word	.LANCHOR0
	.word	.LANCHOR0+2320
	.word	.LC72
.LFE246:
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
.LFB247:
	.loc 2 2570 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL302:
	mov	ip, sp
.LCFI138:
	push	{fp, ip, lr, pc}
.LCFI139:
	sub	fp, ip, #4
.LCFI140:
	.loc 2 2570 0
	uxth	r0, r0
	.loc 2 2571 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L293
.LBB289:
.LBB290:
	.loc 2 2573 0
	ldr	r3, .L294
	mov	r1, #0
	ldr	r2, [r3, #2300]
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	.loc 2 2574 0
	bl	INSERT_FREE_LIST
.LVL303:
.L293:
.LBE290:
.LBE289:
	.loc 2 2576 0
	mov	r0, #0
.LVL304:
	ldmfd	sp, {fp, sp, pc}
.L295:
	.align	2
.L294:
	.word	.LANCHOR0
.LFE247:
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
.LFB250:
	.loc 2 2703 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL305:
	mov	ip, sp
.LCFI141:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI142:
	sub	fp, ip, #4
.LCFI143:
	.loc 2 2707 0
	ldrh	r2, [r0]
	movw	r3, #65535
	.loc 2 2703 0
	mov	r4, r0
	.loc 2 2707 0
	cmp	r2, r3
	bne	.L297
	.loc 2 2707 0 is_stmt 0 discriminator 1
	movw	r2, #2707
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	printk
.LVL306:
.L297:
	.loc 2 2708 0 is_stmt 1
	ldr	r5, .L310+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #78]
	cmp	r2, r3
	bne	.L298
	.loc 2 2708 0 is_stmt 0 discriminator 1
	movw	r2, #2708
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	printk
.LVL307:
.L298:
	.loc 2 2709 0 is_stmt 1
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L299
	.loc 2 2709 0 is_stmt 0 discriminator 1
	movw	r2, #2709
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	printk
.LVL308:
.L299:
	.loc 2 2711 0 is_stmt 1
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	.loc 2 2710 0
	mov	r3, #0
	.loc 2 2714 0
	ldrh	r1, [r5, #8]
	.loc 2 2712 0
	movw	ip, #65535
	.loc 2 2710 0
	strb	r3, [r4, #10]
	.loc 2 2711 0
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
.LVL309:
	.loc 2 2716 0
	mov	r2, r3
.L300:
	.loc 2 2712 0
	cmp	r0, ip
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L302
	.loc 2 2721 0
	ldrh	r2, [r4, #4]
	.loc 2 2720 0
	ldrh	r6, [r4, #2]
	.loc 2 2721 0
	sub	r2, r2, #1
	uxth	r2, r2
	.loc 2 2720 0
	orr	r6, r6, r0, lsl #10
	.loc 2 2728 0
	movw	r0, #65535
.LVL310:
	.loc 2 2721 0
	strh	r2, [r4, #4]	@ movhi
.L304:
	.loc 2 2723 0
	add	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2724 0
	cmp	r1, r3
	.loc 2 2725 0
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2726 0
	moveq	r3, #0
	.loc 2 2728 0
	add	ip, r4, r3, lsl #1
	ldrh	ip, [ip, #16]
	cmp	ip, r0
	beq	.L304
	strb	r3, [r4, #6]
	.loc 2 2730 0
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	ldrh	r1, [r5, #78]
	sub	r3, r3, r1
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L296
	.loc 2 2730 0 is_stmt 0 discriminator 2
	movw	r2, #2730
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	printk
.LVL311:
.L296:
	.loc 2 2732 0 is_stmt 1
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.LVL312:
.L302:
	.loc 2 2713 0
	add	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2714 0
	cmp	r3, r1
	.loc 2 2713 0
	strb	r3, [r4, #6]
	.loc 2 2715 0
	ldrheq	r3, [r4, #2]
	.loc 2 2716 0
	strbeq	r2, [r4, #6]
	.loc 2 2715 0
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2718 0
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #16]
	b	.L300
.L311:
	.align	2
.L310:
	.word	.LANCHOR1+111
	.word	.LC1
	.word	.LANCHOR0
.LFE250:
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
.LFB253:
	.file 5 "drivers/rkflash/rksftl/sftl_gc.c"
	.loc 5 22 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI144:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI145:
	sub	fp, ip, #4
.LCFI146:
	.loc 5 25 0
	ldr	r3, .L317
	.loc 5 27 0
	mov	lr, #12
	mov	r4, #1
	.loc 5 30 0
	mov	r5, #20
	.loc 5 25 0
	mov	r2, #0
	str	r2, [r3, #3140]
.L313:
	.loc 5 26 0 discriminator 1
	ldrh	r1, [r3, #8]
	uxth	r0, r2
	add	ip, r2, #1
	cmp	r0, r1
	bcc	.L314
	.loc 5 35 0 discriminator 1
	mov	ip, #12
	mov	lr, #0
.L315:
	.loc 5 34 0 discriminator 1
	ldr	r2, [r3, #3160]
	cmp	r1, r2
	bcc	.L316
	.loc 5 39 0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L314:
	uxth	r2, r2
	.loc 5 27 0 discriminator 3
	ldr	r6, [r3, #3144]
	mul	r0, lr, r2
	add	r1, r6, r0
	str	r4, [r1, #8]
	.loc 5 28 0 discriminator 3
	ldrh	r1, [r3, #86]
	mul	r1, r2, r1
	add	r7, r1, #3
	cmp	r1, #0
	movlt	r1, r7
	ldr	r7, [r3, #3148]
	bic	r1, r1, #3
	add	r1, r7, r1
	str	r1, [r6, r0]
	.loc 5 29 0 discriminator 3
	ldrh	r1, [r3, #88]
	ldr	r7, [r3, #3144]
	mul	r1, r2, r1
	add	r6, r7, r0
	add	r8, r1, #3
	cmp	r1, #0
	movlt	r1, r8
	ldr	r8, [r3, #3152]
	bic	r1, r1, #3
	add	r1, r8, r1
	str	r1, [r6, #4]
	.loc 5 30 0 discriminator 3
	ldr	r1, [r3, #3156]
	mla	r2, r5, r2, r1
	ldr	r1, [r7, r0]
	str	r1, [r2, #8]
	.loc 5 31 0 discriminator 3
	ldr	r1, [r6, #4]
	str	r1, [r2, #12]
	mov	r2, ip
	b	.L313
.L316:
	.loc 5 35 0 discriminator 3
	mul	r4, ip, r1
	ldr	r5, [r3, #3144]
	add	r2, r5, r4
	str	lr, [r2, #8]
	.loc 5 36 0 discriminator 3
	ldrh	r2, [r3, #86]
	mul	r2, r1, r2
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	ldr	r0, [r3, #3148]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r5, r4]
	.loc 5 37 0 discriminator 3
	ldrh	r2, [r3, #88]
	ldr	r0, [r3, #3144]
	mul	r2, r1, r2
	add	r0, r0, r4
	.loc 5 34 0 discriminator 3
	add	r1, r1, #1
	uxth	r1, r1
	.loc 5 37 0 discriminator 3
	add	r4, r2, #3
	cmp	r2, #0
	movlt	r2, r4
	ldr	r4, [r3, #3152]
	bic	r2, r2, #3
	add	r2, r4, r2
	str	r2, [r0, #4]
	b	.L315
.L318:
	.align	2
.L317:
	.word	.LANCHOR0
.LFE253:
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
.LFB254:
	.loc 5 42 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL313:
	mov	ip, sp
.LCFI147:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI148:
	sub	fp, ip, #4
.LCFI149:
	.loc 5 47 0
	ldr	r3, .L326
	.loc 5 46 0
	mov	lr, #0
	.loc 5 48 0
	mov	r5, #20
	mov	r7, #12
	.loc 5 49 0
	mov	r8, lr
	.loc 5 47 0
	ldr	r6, [r3, #3160]
	.loc 5 48 0
	ldr	r4, [r3, #3144]
.L320:
	uxth	r3, lr
	.loc 5 46 0 discriminator 1
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	.loc 5 48 0
	mla	ip, r5, r3, r0
	mov	r2, #0
.L321:
	uxth	r3, r2
	.loc 5 47 0 discriminator 1
	cmp	r6, r3
	bls	.L322
	.loc 5 48 0
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L321
	.loc 5 49 0
	str	r8, [r9, #8]
.L322:
	add	lr, lr, #1
	b	.L320
.L327:
	.align	2
.L326:
	.word	.LANCHOR0
.LFE254:
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
.LFB255:
	.loc 5 57 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL314:
	mov	ip, sp
.LCFI150:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
.LCFI151:
	sub	fp, ip, #4
.LCFI152:
	.loc 5 62 0
	ldr	r3, .L335
	.loc 5 61 0
	mov	ip, #0
	.loc 5 63 0
	mov	r6, #12
	.loc 5 64 0
	mov	r7, #1
	.loc 5 65 0
	mov	r8, #20
	.loc 5 62 0
	ldr	r4, [r3, #3160]
	.loc 5 63 0
	ldr	r5, [r3, #3144]
.L329:
	uxth	r2, ip
	.loc 5 61 0 discriminator 1
	cmp	r1, r2
	bhi	.L333
	.loc 5 71 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L333:
	mov	lr, #0
.L330:
	uxth	r3, lr
	.loc 5 62 0 discriminator 1
	cmp	r4, r3
	bls	.L331
	.loc 5 63 0
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L330
	.loc 5 65 0
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	.loc 5 64 0
	str	r7, [r3, #8]
	.loc 5 65 0
	str	lr, [r2, #8]
	.loc 5 66 0
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L331:
	add	ip, ip, #1
	b	.L329
.L336:
	.align	2
.L335:
	.word	.LANCHOR0
.LFE255:
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
.LFB256:
	.loc 5 74 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL315:
	mov	ip, sp
.LCFI153:
	push	{fp, ip, lr, pc}
.LCFI154:
	sub	fp, ip, #4
.LCFI155:
	.loc 5 78 0
	ldr	r2, .L342
	.loc 5 74 0
	uxth	r0, r0
	.loc 5 78 0
	ldr	r3, [r2, #3164]
	.loc 5 77 0
	add	r2, r2, #3168
	ldrh	r2, [r2]
	add	r2, r3, r2, lsl #1
.L338:
	.loc 5 77 0 is_stmt 0 discriminator 1
	cmp	r3, r2
	bne	.L340
	.loc 5 81 0 is_stmt 1
	mov	r0, #0
.LVL316:
	ldmfd	sp, {fp, sp, pc}
.LVL317:
.L340:
	.loc 5 78 0
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L338
	.loc 5 79 0
	mov	r0, #1
.LVL318:
	.loc 5 82 0
	ldmfd	sp, {fp, sp, pc}
.L343:
	.align	2
.L342:
	.word	.LANCHOR0
.LFE256:
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
.LFB257:
	.loc 5 85 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL319:
	mov	ip, sp
.LCFI156:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI157:
	sub	fp, ip, #4
.LCFI158:
	.loc 5 85 0
	mov	r4, r0
	.loc 5 88 0
	ubfx	r0, r0, #10, #16
.LVL320:
	.loc 5 85 0
	mov	r5, r1
	mov	r6, r2
	.loc 5 88 0
	bl	P2V_block_in_plane
.LVL321:
	.loc 5 91 0
	ldr	r3, .L348
	mov	ip, #0
	add	lr, r3, #3168
	.loc 5 92 0
	ldr	r1, [r3, #3164]
	.loc 5 91 0
	ldrh	r7, [lr]
	sub	r2, r1, #2
.LVL322:
.L345:
	uxth	r8, ip
.LVL323:
	.loc 5 91 0 is_stmt 0 discriminator 1
	cmp	r8, r7
	bcc	.L347
	.loc 5 97 0 is_stmt 1
	moveq	ip, r8
	lsleq	ip, ip, #1
	strheq	r0, [r1, ip]	@ movhi
	.loc 5 98 0
	ldrheq	r2, [lr]
	addeq	r2, r2, #1
	strheq	r2, [lr]	@ movhi
	b	.L346
.L347:
	.loc 5 92 0
	ldrh	r8, [r2, #2]!
.LVL324:
	add	ip, ip, #1
	cmp	r8, r0
	bne	.L345
.LVL325:
.L346:
	.loc 5 101 0
	movw	ip, #3176
	mov	r0, #12
.LVL326:
	ldrh	r2, [r3, ip]
	mul	r0, r0, r2
	ldr	r2, [r3, #3172]
	add	r1, r2, r0
	.loc 5 102 0
	stmib	r1, {r5, r6}
.LVL327:
	.loc 5 103 0
	str	r4, [r2, r0]
	.loc 5 107 0
	ldrh	r2, [r3, ip]
	add	r2, r2, #1
	strh	r2, [r3, ip]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L349:
	.align	2
.L348:
	.word	.LANCHOR0
.LFE257:
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
.LFB264:
	.loc 5 345 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL328:
	mov	ip, sp
.LCFI159:
	push	{r4, fp, ip, lr, pc}
.LCFI160:
	sub	fp, ip, #4
.LCFI161:
	.loc 5 345 0
	uxth	r4, r0
.LVL329:
	.loc 5 347 0
	ldr	r0, .L353
	mov	r1, r4
	bl	printk
.LVL330:
	.loc 5 349 0
	ldr	r3, .L353+4
	movw	ip, #3178
	ldrh	lr, [r3, ip]
	cmp	r4, lr
	beq	.L351
	.loc 5 349 0 is_stmt 0 discriminator 1
	movw	r2, #3180
	ldrh	r0, [r3, r2]
	cmp	r4, r0
	beq	.L351
	.loc 5 351 0 is_stmt 1
	movw	r1, #65535
	cmp	lr, r1
	.loc 5 352 0
	strheq	r4, [r3, ip]	@ movhi
	.loc 5 351 0
	beq	.L351
	.loc 5 353 0
	cmp	r0, r1
	.loc 5 354 0
	strheq	r4, [r3, r2]	@ movhi
.L351:
	.loc 5 356 0
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L354:
	.align	2
.L353:
	.word	.LC73
	.word	.LANCHOR0
.LFE264:
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
.LFB265:
	.loc 5 359 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL331:
	mov	ip, sp
.LCFI162:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI163:
	sub	fp, ip, #4
.LCFI164:
	.loc 5 364 0
	ldr	r4, .L359
	.loc 5 359 0
	uxth	r5, r0
.LVL332:
	.loc 5 364 0
	movw	r6, #3182
	.loc 5 361 0
	mov	r0, r5
	bl	P2V_block_in_plane
.LVL333:
	.loc 5 364 0
	mov	r2, r5
	.loc 5 361 0
	mov	r7, r0
	.loc 5 364 0
	ldrh	r1, [r4, r6]
	ldr	r0, .L359+4
	bl	printk
.LVL334:
	.loc 5 366 0
	mov	r0, r7
	bl	FtlGcRefreshBlock
.LVL335:
	.loc 5 368 0
	ldrh	r3, [r4, r6]
	mov	r2, #0
	.loc 5 369 0
	add	r0, r4, #3184
.L356:
	.loc 5 368 0 discriminator 1
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L358
	.loc 5 372 0
	cmp	r3, #15
	.loc 5 373 0
	movwls	r2, #3182
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L359+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L357
.L358:
	add	r2, r2, #1
	.loc 5 369 0
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L356
.L357:
	.loc 5 375 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L360:
	.align	2
.L359:
	.word	.LANCHOR0
	.word	.LC74
	.word	.LANCHOR0+3184
.LFE265:
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
.LFB266:
	.loc 5 379 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI165:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI166:
	sub	fp, ip, #4
.LCFI167:
	.loc 5 380 0
	ldr	r4, .L367
	movw	r3, #3182
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L362
	.loc 5 380 0 is_stmt 0 discriminator 1
	movw	r3, #3178
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L362
	.loc 5 381 0 is_stmt 1
	movw	r3, #3218
.LBB293:
.LBB294:
	.loc 5 383 0
	movw	r5, #3218
.LBE294:
.LBE293:
	.loc 5 381 0
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	.loc 5 382 0
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
.LBB296:
.LBB295:
	.loc 5 383 0
	ldrh	r3, [r4, r5]
	ldr	r2, .L367+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
.LVL336:
	bl	FtlGcRefreshBlock
.LVL337:
	.loc 5 384 0
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L362:
.LBE295:
.LBE296:
	.loc 5 387 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L368:
	.align	2
.L367:
	.word	.LANCHOR0
	.word	.LANCHOR0+3184
.LFE266:
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
.LFB272:
	.loc 1 44 0
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL338:
	mov	ip, sp
.LCFI168:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI169:
	sub	fp, ip, #4
.LCFI170:
	sub	sp, sp, #12
	.loc 1 50 0
	mov	r6, #0
.LVL339:
	.loc 1 46 0
	ldr	r5, .L387
	.loc 1 44 0
	mov	r8, r1
	.loc 1 51 0
	ldr	r9, .L387+4
	mov	r4, r0
	ldr	r10, .L387+8
	.loc 1 46 0
	ldrh	r3, [r5, #12]
.LVL340:
	str	r3, [fp, #-52]
.LVL341:
.L370:
	.loc 1 50 0 discriminator 1
	cmp	r6, r8
	bne	.L377
	.loc 1 71 0
	mov	r0, #0
.LVL342:
	sub	sp, fp, #40
.LCFI171:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL343:
.L377:
.LCFI172:
	.loc 1 51 0
	ldr	r3, [r4, #8]
.LVL344:
	cmp	r3, #0
	beq	.L371
	.loc 1 51 0 is_stmt 0 discriminator 2
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L372
.L371:
	.loc 1 51 0 discriminator 3
	mov	r2, #51
.LVL345:
	mov	r1, r9
	mov	r0, r10
.LVL346:
	bl	printk
.LVL347:
.L372:
	.loc 1 53 0 is_stmt 1
	ldr	r7, .L387+12
	.loc 1 52 0
	sub	r2, fp, #48
.LVL348:
	sub	r1, fp, #44
	mov	r0, r4
.LVL349:
	bl	l2p_addr_tran
.LVL350:
	.loc 1 53 0
	ldr	r3, [r4, #12]
	ldr	ip, [r7, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-44]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	blx	ip
.LVL351:
	.loc 1 57 0
	str	r0, [r4]
	.loc 1 58 0
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L374
	.loc 1 59 0
	ldr	r0, [fp, #-52]
.LVL352:
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-44]
	ldr	ip, [r7, #12]
	add	r3, r3, #8
	add	r2, r2, #2048
	add	r1, r0, r1
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
.LVL353:
	blx	ip
.LVL354:
	.loc 1 63 0
	cmn	r0, #1
	beq	.L375
	.loc 1 64 0 discriminator 1
	ldr	r3, [r4, #12]
	.loc 1 63 0 discriminator 1
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L374
	.loc 1 64 0
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L374
	.loc 1 65 0
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L374
.L375:
	.loc 1 67 0
	mvn	r3, #0
	str	r3, [r4]
.L374:
	.loc 1 50 0 discriminator 2
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L370
.L388:
	.align	2
.L387:
	.word	g_nand_phy_info
	.word	.LANCHOR1+130
	.word	.LC1
	.word	g_nand_ops
.LFE272:
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
.LFB180:
	.loc 4 279 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI173:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI174:
	sub	fp, ip, #4
.LCFI175:
	.loc 4 286 0
	ldr	r4, .L399
	.loc 4 289 0
	mov	r6, #0
.LVL355:
	.loc 4 290 0
	mvn	r9, #0
	.loc 4 294 0
	ldr	r10, .L399+4
	.loc 4 286 0
	ldr	r3, [r4, #3240]
	add	r7, r4, #138
	.loc 4 287 0
	ldr	r8, [r4, #3244]
	.loc 4 286 0
	str	r3, [r4, #3228]
	.loc 4 287 0
	str	r8, [r4, #3232]
.LVL356:
.L390:
	.loc 4 289 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r6, r3
	bcc	.L395
	.loc 4 314 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L395:
	.loc 4 291 0
	ldrh	r5, [r4, #74]
.LVL357:
	.loc 4 290 0
	strh	r9, [r7, #2]!	@ movhi
.LVL358:
.L392:
	.loc 4 291 0 discriminator 2
	ldrh	r3, [r4, #74]
	sub	r5, r5, #1
.LVL359:
	uxth	r5, r5
.LVL360:
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L393
	.loc 4 292 0
	mla	r3, r6, r3, r5
	.loc 4 294 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	.loc 4 292 0
	lsl	r3, r3, #10
.LVL361:
	.loc 4 293 0
	str	r3, [r4, #3224]
	.loc 4 294 0
	bl	FlashReadPages
.LVL362:
	.loc 4 295 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	beq	.L392
	.loc 4 301 0
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L392
	.loc 4 303 0
	strh	r5, [r7]	@ movhi
.L393:
	.loc 4 289 0 discriminator 2
	add	r6, r6, #1
	b	.L390
.L400:
	.align	2
.L399:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
.LFE180:
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
.LFB186:
	.loc 2 47 0
	@ args = 0, pretend = 0, frame = 84
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL363:
	mov	ip, sp
.LCFI176:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI177:
	sub	fp, ip, #4
.LCFI178:
	sub	sp, sp, #84
	.loc 2 51 0
	cmp	r1, #1
	ldr	r3, .L409
	.loc 2 57 0
	lsl	r6, r0, #10
	.loc 2 58 0
	mov	r2, r1
	.loc 2 47 0
	mov	r7, r1
	.loc 2 58 0
	sub	r0, fp, #116
.LVL364:
	.loc 2 51 0
	ldrheq	r4, [r3, #80]
	.loc 2 58 0
	mov	r1, #1
.LVL365:
	.loc 2 51 0
	ldrhne	r4, [r3, #78]
	.loc 2 53 0
	ldr	r3, .L409+4
	.loc 2 56 0
	sub	r4, r4, #1
	sxth	r5, r4
.LVL366:
	.loc 2 53 0
	str	r3, [fp, #-108]
	.loc 2 54 0
	sub	r3, fp, #96
	str	r3, [fp, #-104]
	.loc 2 57 0
	orr	r3, r5, r6
	str	r3, [fp, #-112]
	.loc 2 58 0
	bl	FlashReadPages
.LVL367:
	.loc 2 59 0
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	moveq	r8, #0
.LVL368:
	beq	.L405
.L404:
	.loc 2 73 0
	mov	r0, r5
	sub	sp, fp, #32
.LCFI179:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL369:
.L408:
.LCFI180:
	.loc 2 61 0
	add	r4, r8, r5
	.loc 2 63 0
	mov	r2, r7
	.loc 2 61 0
	add	r4, r4, r4, lsr #31
	.loc 2 63 0
	mov	r1, #1
	sub	r0, fp, #116
	.loc 2 61 0
	asr	r4, r4, #1
	.loc 2 62 0
	sxth	r3, r4
	orr	r3, r3, r6
	str	r3, [fp, #-112]
	.loc 2 63 0
	bl	FlashReadPages
.LVL370:
	.loc 2 65 0
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	bne	.L406
	.loc 2 65 0 is_stmt 0 discriminator 1
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	.loc 2 66 0 is_stmt 1 discriminator 1
	subeq	r4, r4, #1
	sxtheq	r5, r4
	.loc 2 65 0 discriminator 1
	beq	.L405
.L406:
	.loc 2 68 0
	add	r4, r4, #1
	sxth	r8, r4
.L405:
	.loc 2 60 0
	cmp	r8, r5
	ble	.L408
	b	.L404
.L410:
	.align	2
.L409:
	.word	.LANCHOR0
	.word	ftl_temp_buf
.LFE186:
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
.LFB273:
	.loc 1 74 0
	@ args = 0, pretend = 0, frame = 36
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL371:
	mov	ip, sp
.LCFI181:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI182:
	sub	fp, ip, #4
.LCFI183:
	sub	sp, sp, #36
	.loc 1 74 0
	mov	r8, r3
	.loc 1 76 0
	ldr	r3, .L441
.LVL372:
	.loc 1 74 0
	mov	r7, r2
	.loc 1 81 0
	ldr	r10, .L441+4
	.loc 1 74 0
	mov	r9, r1
	mov	r4, r0
	.loc 1 80 0
	mov	r5, r0
.LVL373:
	.loc 1 76 0
	ldrh	r2, [r3, #12]
.LVL374:
	.loc 1 80 0
	mov	r6, #0
	str	r3, [fp, #-76]
	.loc 1 76 0
	str	r2, [fp, #-72]
.LVL375:
.L412:
	.loc 1 80 0 discriminator 1
	cmp	r6, r9
	bne	.L420
	.loc 1 101 0
	cmp	r8, #0
	bne	.L427
.L440:
	.loc 1 131 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI184:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL376:
.L420:
.LCFI185:
	.loc 1 81 0
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L413
	.loc 1 81 0 is_stmt 0 discriminator 2
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L414
.L413:
	.loc 1 81 0 discriminator 3
	mov	r2, #81
.LVL377:
	ldr	r1, .L441+8
	mov	r0, r10
	bl	printk
.LVL378:
.L414:
	.loc 1 82 0 is_stmt 1
	sub	r2, fp, #68
.LVL379:
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
.LVL380:
	.loc 1 83 0
	ldr	r3, .L441+12
	ldr	r2, [r5, #8]
	ldr	r1, [fp, #-64]
	ldr	ip, [r3, #8]
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	ldr	r3, [r5, #12]
	blx	ip
.LVL381:
	.loc 1 88 0
	cmp	r0, #0
	.loc 1 89 0
	mvnne	r3, #0
	.loc 1 87 0
	streq	r0, [r5]
	.loc 1 89 0
	strne	r3, [r5]
	.loc 1 91 0
	ldr	r3, [fp, #-76]
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L418
	.loc 1 92 0
	ldr	r1, .L441+12
	ldr	r0, [fp, #-72]
.LVL382:
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	ip, [r1, #8]
	ldr	r1, [fp, #-64]
	add	r3, r3, #8
	add	r2, r2, #2048
	add	r1, r0, r1
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
.LVL383:
	blx	ip
.LVL384:
	.loc 1 96 0
	cmp	r0, #0
	.loc 1 97 0
	mvnne	r3, #0
	strne	r3, [r5]
.L418:
	.loc 1 80 0 discriminator 2
	add	r6, r6, #1
	add	r5, r5, #20
	b	.L412
.LVL385:
.L425:
.LBB297:
	.loc 1 106 0
	ldr	r9, .L441+16
.LVL386:
	mov	r3, #0
	.loc 1 107 0
	str	r3, [r8, #3248]
	.loc 1 112 0
	mov	r2, r7
.LVL387:
	mov	r1, #1
.LVL388:
	sub	r0, fp, #60
	.loc 1 106 0
	str	r3, [r9]
	.loc 1 108 0
	ldr	r3, [r4, #4]
	.loc 1 109 0
	str	r9, [fp, #-52]
	.loc 1 110 0
	str	r10, [fp, #-48]
	.loc 1 108 0
	str	r3, [fp, #-56]
	.loc 1 112 0
	bl	FlashReadPages
.LVL389:
	.loc 1 113 0
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	bne	.L422
	.loc 1 114 0
	ldr	r1, [r4, #4]
	ldr	r0, .L441+20
	str	r3, [fp, #-72]
	bl	printk
.LVL390:
	.loc 1 115 0
	ldr	r3, [fp, #-72]
	str	r3, [r4]
.L422:
	.loc 1 117 0
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L423
	.loc 1 118 0
	ldr	r2, [r3]
	ldr	r3, [r8, #3248]
	cmp	r2, r3
	beq	.L423
	.loc 1 119 0
	ldr	r1, [r4, #4]
	ldr	r0, .L441+24
	bl	printk
.LVL391:
	.loc 1 120 0
	mvn	r3, #0
	str	r3, [r4]
.L423:
	.loc 1 123 0
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L424
	.loc 1 124 0
	ldr	r2, [r3]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L424
	.loc 1 125 0
	ldr	r1, [r4, #4]
	ldr	r0, .L441+28
	bl	printk
.LVL392:
	.loc 1 126 0
	mvn	r3, #0
	str	r3, [r4]
.L424:
.LBE297:
	.loc 1 103 0 discriminator 2
	add	r5, r5, #1
	add	r4, r4, #20
.LVL393:
.L421:
	.loc 1 103 0 is_stmt 0 discriminator 1
	cmp	r6, r5
	bne	.L425
	b	.L440
.LVL394:
.L427:
.LBB298:
	.loc 1 107 0 is_stmt 1
	ldr	r8, .L441+32
.LVL395:
.LBE298:
	mov	r5, #0
.LBB299:
	.loc 1 110 0
	add	r10, r8, #3248
	b	.L421
.L442:
	.align	2
.L441:
	.word	g_nand_phy_info
	.word	.LC1
	.word	.LANCHOR1+145
	.word	g_nand_ops
	.word	check_buf
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR0
.LBE299:
.LFE273:
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
.LFB274:
	.loc 1 134 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL396:
	mov	ip, sp
.LCFI186:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
.LCFI187:
	sub	fp, ip, #4
.LCFI188:
	sub	sp, sp, #8
	.loc 1 140 0
	mov	r5, #0
.LVL397:
	.loc 1 137 0
	ldr	r3, .L454
	.loc 1 134 0
	mov	r8, r2
	.loc 1 142 0
	ldr	r7, .L454+4
	mov	r4, r0
	.loc 1 137 0
	ldrh	r9, [r3, #12]
.LVL398:
	mov	r6, r3
.LVL399:
.L444:
	.loc 1 140 0 discriminator 1
	cmp	r5, r8
	bne	.L450
	.loc 1 153 0
	mov	r0, #0
.LVL400:
	sub	sp, fp, #36
.LCFI189:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL401:
.L450:
.LCFI190:
	.loc 1 141 0
	sub	r2, fp, #44
	sub	r1, fp, #40
.LVL402:
	mov	r0, r4
.LVL403:
	bl	l2p_addr_tran
.LVL404:
	.loc 1 142 0
	ldr	r3, [r7, #4]
	ldr	r1, [fp, #-40]
	ldrb	r0, [fp, #-44]	@ zero_extendqisi2
	blx	r3
.LVL405:
	.loc 1 144 0
	cmp	r0, #0
	.loc 1 145 0
	mvnne	r3, #0
	.loc 1 143 0
	streq	r0, [r4]
	.loc 1 145 0
	strne	r3, [r4]
	.loc 1 146 0
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L448
	.loc 1 147 0
	ldr	r1, [fp, #-40]
	ldr	r3, [r7, #4]
	ldrb	r0, [fp, #-44]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r3
.LVL406:
	.loc 1 148 0
	cmp	r0, #0
	.loc 1 149 0
	mvnne	r3, #0
	strne	r3, [r4]
.L448:
	.loc 1 140 0 discriminator 2
	add	r5, r5, #1
	add	r4, r4, #20
	b	.L444
.L455:
	.align	2
.L454:
	.word	g_nand_phy_info
	.word	g_nand_ops
.LFE274:
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
.LFB190:
	.loc 2 98 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL407:
	mov	ip, sp
.LCFI191:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI192:
	sub	fp, ip, #4
.LCFI193:
	.loc 2 98 0
	uxth	r5, r0
.LVL408:
	.loc 2 99 0
	movw	r2, #65533
	.loc 2 98 0
	uxth	r1, r1
	.loc 2 99 0
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	ldmfdhi	sp, {r4, r5, r6, fp, sp, pc}
.LBB308:
.LBB309:
	.loc 2 94 0
	ldr	r4, .L465
.LBE309:
.LBE308:
	.loc 2 101 0
	ldrh	r3, [r4, #194]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
.LBB310:
.LBB311:
	.loc 2 103 0
	cmp	r1, #0
	beq	.L458
.LBB312:
	.loc 2 104 0
	mov	r0, r5
	bl	P2V_block_in_plane
.LVL409:
	mov	r6, r0
	.loc 2 105 0
	ldr	r0, [r4, #3760]
	lsl	r3, r5, #10
	.loc 2 106 0
	mov	r2, #1
	mov	r1, r2
	.loc 2 105 0
	str	r3, [r0, #4]
	.loc 2 106 0
	bl	FlashEraseBlocks
.LVL410:
	.loc 2 107 0
	ldr	r2, [r4, #2304]
	lsl	r0, r6, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	.loc 2 108 0
	ldr	r3, [r4, #2536]
	add	r3, r3, #1
	str	r3, [r4, #2536]
.LVL411:
.L458:
.LBE312:
	.loc 2 110 0
	ldrh	r3, [r4, #194]
	add	r3, r3, #1
	strh	r3, [r4, #194]	@ movhi
	.loc 2 111 0
	ldrh	r3, [r4, #192]
	add	r2, r4, r3, lsl #1
	.loc 2 112 0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 111 0
	strh	r5, [r2, #196]	@ movhi
	.loc 2 112 0
	strh	r3, [r4, #192]	@ movhi
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L466:
	.align	2
.L465:
	.word	.LANCHOR0
.LBE311:
.LBE310:
.LFE190:
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
.LFB159:
	.loc 3 528 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL412:
	mov	ip, sp
.LCFI194:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI195:
	sub	fp, ip, #4
.LCFI196:
	sub	sp, sp, #24
	.loc 3 528 0
	uxth	r3, r0
.LVL413:
	.loc 3 539 0
	mov	r10, #0
	.loc 3 528 0
	uxtb	r6, r1
.LVL414:
	str	r3, [fp, #-48]
	.loc 3 536 0
	mov	r5, r10
	.loc 3 538 0
	ldr	r3, .L509
.LVL415:
	.loc 3 537 0
	mov	r4, r10
.LVL416:
	.loc 3 538 0
	ldr	r2, [fp, #-48]
.LVL417:
	.loc 3 540 0
	mov	r9, #20
	mov	r8, r3
	.loc 3 538 0
	str	r2, [r3, #3764]
.L468:
	.loc 3 539 0 discriminator 1
	ldrh	r1, [r8, #8]
	uxth	r2, r10
.LVL418:
	cmp	r1, r2
	bhi	.L472
	.loc 3 559 0
	cmp	r5, #0
	beq	.L467
	.loc 3 563 0
	mov	r7, #0
	.loc 3 564 0
	mov	r9, #20
	.loc 3 562 0
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r8, #3760]
	bl	FlashEraseBlocks
.LVL419:
.L475:
	.loc 3 563 0 discriminator 1
	uxth	r2, r7
	cmp	r5, r2
	bhi	.L477
.L478:
	.loc 3 571 0
	cmp	r6, #0
	.loc 3 573 0
	ldrhne	r3, [r8, #80]
.LVL420:
	.loc 3 533 0
	moveq	r3, #6
.LVL421:
	.loc 3 578 0
	ldr	r8, .L509
	.loc 3 533 0
	streq	r3, [fp, #-60]
	.loc 3 531 0
	moveq	r3, #1
.LVL422:
	.loc 3 573 0
	strne	r3, [fp, #-52]
	.loc 3 574 0
	lsrne	r3, r3, #2
.LVL423:
	mov	r10, r8
.LVL424:
	.loc 3 531 0
	streq	r3, [fp, #-52]
	.loc 3 574 0
	strne	r3, [fp, #-60]
	.loc 3 572 0
	movne	r3, #1
.LVL425:
	strne	r3, [fp, #-56]
	mov	r3, #0
.LVL426:
	.loc 3 534 0
	streq	r6, [fp, #-56]
	str	r3, [fp, #-44]
.LVL427:
.L487:
	.loc 3 578 0
	mov	r7, #0
	.loc 3 577 0
	mov	r5, r7
.L479:
	.loc 3 578 0 discriminator 1
	ldrh	r1, [r8, #8]
	uxth	r3, r7
.LVL428:
	cmp	r1, r3
	bhi	.L482
	.loc 3 593 0
	cmp	r5, #0
	beq	.L467
	.loc 3 596 0
	ldr	r2, [fp, #-56]
.LVL429:
	mov	r3, #1
	mov	r1, r5
	ldr	r0, [r8, #3760]
	bl	FlashProgPages
.LVL430:
	.loc 3 597 0
	mov	r9, #0
	.loc 3 598 0
	mov	r2, #20
.L484:
	.loc 3 597 0 discriminator 1
	uxth	r3, r9
	cmp	r5, r3
	bhi	.L486
	.loc 3 604 0
	ldr	r3, [fp, #-44]
.LVL431:
	ldr	r2, [fp, #-60]
.LVL432:
	add	r7, r3, r2
	uxth	r3, r7
	str	r3, [fp, #-44]
	.loc 3 605 0
	ldr	r2, [fp, #-44]
.LVL433:
	ldr	r3, [fp, #-52]
.LVL434:
	cmp	r3, r2
	bhi	.L487
	mov	r7, #0
	.loc 3 611 0 discriminator 1
	mov	r9, #20
.L488:
	.loc 3 610 0 discriminator 1
	uxth	r3, r7
.LVL435:
	cmp	r5, r3
	bhi	.L490
	.loc 3 615 0
	ldr	r3, [fp, #-48]
.LVL436:
	adds	r6, r6, #0
.LVL437:
	movne	r6, #1
	cmp	r3, #63
	movhi	r8, r6
	orrls	r8, r6, #1
	cmp	r8, #0
	beq	.L467
	.loc 3 616 0
	mov	r2, r5
.LVL438:
	ldr	r1, [fp, #-56]
	ldr	r0, [r10, #3760]
	bl	FlashEraseBlocks
.LVL439:
.L467:
	.loc 3 619 0
	mov	r0, r4
	sub	sp, fp, #40
.LCFI197:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL440:
.L472:
.LCFI198:
	.loc 3 540 0
	uxth	r2, r10
	ldr	r0, [r8, #3760]
	mov	r3, #0
	mul	r1, r9, r2
	.loc 3 541 0
	add	r2, r8, r2
	.loc 3 540 0
	str	r3, [r0, r1]
	.loc 3 541 0
	ldr	r1, [fp, #-48]
	ldrb	r0, [r2, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL441:
	.loc 3 543 0
	cmp	r6, #0
	.loc 3 541 0
	mov	r7, r0
	.loc 3 543 0
	beq	.L469
	.loc 3 544 0
	bl	IsBlkInVendorPart
.LVL442:
	cmp	r0, #0
	bne	.L470
.L469:
	.loc 3 548 0
	mov	r0, r7
	bl	FtlBbmIsBadBlock
.LVL443:
	cmp	r0, #0
	.loc 3 554 0
	addne	r4, r4, #1
.LVL444:
	uxthne	r4, r4
.LVL445:
	.loc 3 548 0
	bne	.L470
	.loc 3 549 0
	ldr	r1, [r8, #3760]
	lsl	r7, r7, #10
	.loc 3 550 0
	ldr	r2, [r8, #3768]
	.loc 3 549 0
	mla	r1, r9, r5, r1
	.loc 3 550 0
	str	r2, [r1, #8]
	.loc 3 551 0
	ldrh	r2, [r8, #88]
	.loc 3 549 0
	str	r7, [r1, #4]
	.loc 3 551 0
	mul	r2, r5, r2
	.loc 3 552 0
	add	r5, r5, #1
	uxth	r5, r5
	.loc 3 551 0
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	ldr	r0, [r8, #3772]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #12]
.L470:
	add	r10, r10, #1
	b	.L468
.L477:
	.loc 3 564 0
	mul	r2, r9, r7
	ldr	r1, [r8, #3760]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L476
	.loc 3 566 0
	ldr	r0, [r0, #4]
	.loc 3 565 0
	add	r4, r4, #1
.LVL446:
	uxth	r4, r4
.LVL447:
	.loc 3 566 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL448:
.L476:
	add	r7, r7, #1
	b	.L475
.LVL449:
.L482:
	.loc 3 579 0
	uxth	r3, r7
	mov	r2, #20
.LVL450:
	ldr	r0, [r8, #3760]
	mul	r1, r2, r3
	mov	r2, #0
	.loc 3 580 0
	add	r3, r8, r3
	.loc 3 579 0
	str	r2, [r0, r1]
	.loc 3 580 0
	ldr	r1, [fp, #-48]
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL451:
	.loc 3 581 0
	cmp	r6, #0
	.loc 3 580 0
	mov	r9, r0
	.loc 3 581 0
	beq	.L480
	.loc 3 582 0
	bl	IsBlkInVendorPart
.LVL452:
	cmp	r0, #0
	bne	.L481
.L480:
	.loc 3 585 0
	mov	r0, r9
	bl	FtlBbmIsBadBlock
.LVL453:
	cmp	r0, #0
	bne	.L481
	.loc 3 586 0
	ldr	r1, [r8, #3760]
	mov	r3, #20
	mla	r1, r3, r5, r1
	ldr	r3, [fp, #-44]
.LVL454:
	add	r9, r3, r9, lsl #10
	.loc 3 587 0
	ldr	r3, [r8, #3776]
.LVL455:
	.loc 3 586 0
	str	r9, [r1, #4]
	.loc 3 587 0
	str	r3, [r1, #8]
	.loc 3 588 0
	ldrh	r3, [r8, #88]
	mul	r3, r5, r3
	.loc 3 589 0
	add	r5, r5, #1
	uxth	r5, r5
	.loc 3 588 0
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r8, #3768]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L481:
	add	r7, r7, #1
	b	.L479
.L486:
	.loc 3 598 0
	mul	r3, r2, r9
	ldr	r1, [r10, #3760]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L485
	.loc 3 600 0
	ldr	r0, [r0, #4]
	.loc 3 599 0
	add	r4, r4, #1
.LVL456:
	str	r2, [fp, #-64]
	uxth	r4, r4
.LVL457:
	.loc 3 600 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL458:
	ldr	r2, [fp, #-64]
.L485:
	add	r9, r9, #1
	b	.L484
.LVL459:
.L490:
	.loc 3 611 0
	cmp	r6, #0
	beq	.L489
	.loc 3 611 0 is_stmt 0 discriminator 1
	mul	r3, r9, r7
	ldr	r2, [r10, #3760]
.LVL460:
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L489
	.loc 3 612 0 is_stmt 1
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.LVL461:
.L489:
	add	r7, r7, #1
	b	.L488
.L510:
	.align	2
.L509:
	.word	.LANCHOR0
.LFE159:
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
.LFB193:
	.loc 2 181 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI199:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI200:
	sub	fp, ip, #4
.LCFI201:
.LBB313:
.LBB314:
	.loc 2 89 0
	ldr	r4, .L518
.LBE314:
.LBE313:
	.loc 2 203 0
	ldr	r6, .L518+4
.L512:
.LBB316:
.LBB315:
	.loc 2 89 0
	ldrh	r1, [r4, #194]
.LBE315:
.LBE316:
	.loc 2 186 0
	cmp	r1, #0
	beq	.L513
	.loc 2 187 0
	ldrh	r3, [r4, #190]
	.loc 2 188 0
	sub	r1, r1, #1
	strh	r1, [r4, #194]	@ movhi
	.loc 2 187 0
	add	r2, r4, r3, lsl #1
	.loc 2 189 0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 187 0
	ldrh	r5, [r2, #196]
	.loc 2 189 0
	strh	r3, [r4, #190]	@ movhi
.LBB317:
	.loc 2 191 0
	mov	r0, r5
	bl	P2V_block_in_plane
.LVL462:
	mov	r7, r0
	.loc 2 192 0
	ldr	r0, [r4, #3760]
	lsl	r3, r5, #10
	.loc 2 193 0
	mov	r2, #1
	mov	r1, r2
	.loc 2 192 0
	str	r3, [r0, #4]
	.loc 2 193 0
	bl	FlashEraseBlocks
.LVL463:
	.loc 2 194 0
	ldr	r2, [r4, #2304]
	lsl	r0, r7, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.LBE317:
	.loc 2 202 0
	movw	r2, #65533
.LBB318:
	.loc 2 195 0
	ldr	r3, [r4, #2536]
	add	r3, r3, #1
	str	r3, [r4, #2536]
.LBE318:
	.loc 2 202 0
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L517
	.loc 2 203 0
	ldrh	r2, [r4, #194]
	mov	r1, r5
	mov	r0, r6
	bl	printk
.LVL464:
	.loc 2 205 0
	b	.L512
.L513:
	.loc 2 199 0
	ldr	r0, .L518+8
	bl	printk
.LVL465:
.L516:
	b	.L516
.L517:
	.loc 2 208 0
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L519:
	.align	2
.L518:
	.word	.LANCHOR0
	.word	.LC79
	.word	.LC78
.LFE193:
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
.LFB205:
	.loc 2 562 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL466:
	mov	ip, sp
.LCFI202:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI203:
	sub	fp, ip, #4
.LCFI204:
	.loc 2 566 0
	ldrh	r1, [r0, #10]
	.loc 2 562 0
	mov	r4, r0
	.loc 2 566 0
	mov	r3, #0
	ldr	r2, [r0, #12]
.LVL467:
.L521:
	uxth	r5, r3
	.loc 2 566 0 is_stmt 0 discriminator 1
	cmp	r5, r1
	bcs	.L524
	.loc 2 567 0 is_stmt 1
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L521
	.loc 2 568 0
	bl	FtlFreeSysBlkQueueOut
.LVL468:
	.loc 2 569 0
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	.loc 2 568 0
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	.loc 2 569 0
	cmp	r3, r2
	bls	.L522
	.loc 2 570 0
	ldr	r3, .L528
	ldr	r0, .L528+4
	ldrh	r2, [r3, #194]
	bl	printk
.LVL469:
.L523:
	b	.L523
.L522:
	.loc 2 576 0
	ldr	r3, [r4, #28]
	.loc 2 574 0
	strh	r6, [r4, #2]	@ movhi
	.loc 2 575 0
	strh	r5, [r4]	@ movhi
	.loc 2 576 0
	add	r3, r3, #1
	str	r3, [r4, #28]
	.loc 2 577 0
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.LVL470:
.L524:
	.loc 2 581 0
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L526
.LBB321:
.LBB322:
	movw	r2, #581
	ldr	r1, .L528+8
	ldr	r0, .L528+12
	bl	printk
.LVL471:
.L526:
.LBE322:
.LBE321:
	.loc 2 583 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L529:
	.align	2
.L528:
	.word	.LANCHOR0
	.word	.LC80
	.word	.LANCHOR1+160
	.word	.LC1
.LFE205:
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
.LFB275:
	.loc 1 156 0
	@ args = 0, pretend = 0, frame = 84
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL472:
	mov	ip, sp
.LCFI205:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI206:
	sub	fp, ip, #4
.LCFI207:
	sub	sp, sp, #84
	.loc 1 156 0
	uxth	r4, r0
.LVL473:
	.loc 1 161 0
	cmp	r4, #11
	.loc 1 162 0
	movls	r4, #0
.LVL474:
	.loc 1 161 0
	bls	.L530
.LBB335:
.LBB336:
	.loc 1 165 0
	ldr	r5, .L533
	.loc 1 166 0
	sub	r0, fp, #84
.LBB337:
.LBB338:
.LBB339:
	.loc 1 202 0
	mov	r2, #32
	mov	r1, #165
.LBE339:
.LBE338:
.LBE337:
	.loc 1 166 0
	str	r0, [fp, #-92]
.LVL475:
	.loc 1 169 0
	lsl	r4, r4, #10
.LVL476:
	.loc 1 165 0
	str	r5, [fp, #-96]
.LBB342:
.LBB341:
.LBB340:
	.loc 1 202 0
	bl	memset
.LVL477:
.LBE340:
.LBE341:
.LBE342:
.LBB343:
.LBB344:
.LBB345:
	ldr	r3, .L533+4
.LBE345:
.LBE344:
.LBE343:
	.loc 1 170 0
	mov	r2, #1
	mov	r1, r2
	sub	r0, fp, #104
	.loc 1 169 0
	str	r4, [fp, #-100]
.LBB348:
.LBB347:
.LBB346:
	.loc 1 202 0
	str	r3, [r5]
	str	r3, [r5, #4]
.LBE346:
.LBE347:
.LBE348:
	.loc 1 170 0
	bl	FlashEraseBlocks
.LVL478:
	.loc 1 171 0
	mov	r3, #1
	sub	r0, fp, #104
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL479:
	.loc 1 172 0
	ldr	r4, [fp, #-104]
	.loc 1 178 0
	mov	r2, #1
	mov	r1, #0
	sub	r0, fp, #104
	.loc 1 172 0
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
	.loc 1 178 0
	bl	FlashEraseBlocks
.LVL480:
.L530:
.LBE336:
.LBE335:
	.loc 1 180 0
	mov	r0, r4
	sub	sp, fp, #20
.LCFI208:
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L534:
	.align	2
.L533:
	.word	ftl_temp_buf
	.word	1515870810
.LFE275:
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashGetBadBlockList
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
.LFB276:
	.loc 1 183 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL481:
	mov	ip, sp
.LCFI209:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI210:
	sub	fp, ip, #4
.LCFI211:
.LBB359:
.LBB360:
.LBB361:
	.loc 1 202 0
	mov	r2, #256
.LBE361:
.LBE360:
.LBE359:
	.loc 1 183 0
	mov	r5, r1
.LBB366:
.LBB364:
.LBB362:
	.loc 1 202 0
	mov	r1, #255
.LVL482:
.LBE362:
.LBE364:
.LBE366:
	.loc 1 183 0
	mov	r4, r0
.LBB367:
.LBB365:
.LBB363:
	.loc 1 202 0
	bl	memset
.LVL483:
.LBE363:
.LBE365:
.LBE367:
	.loc 1 187 0
	ldr	r3, .L542
	mov	r1, r5
	mov	r0, r4
	ldr	r3, [r3]
	blx	r3
.LVL484:
	uxth	r0, r0
.LVL485:
	.loc 1 188 0
	cmp	r0, #50
	bls	.L536
.LBB368:
.LBB369:
.LBB370:
	.loc 1 202 0
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
.LVL486:
	bl	memset
.LVL487:
.LBE370:
.LBE369:
.LBE368:
	.loc 1 190 0
	mov	r0, #0
.LVL488:
.L536:
	.loc 1 192 0
	ldr	r3, .L542+4
.LVL489:
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	moveq	r3, r4
.LVL490:
	addeq	r1, r3, r0, lsl #1
	beq	.L538
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L539:
	.loc 1 194 0 discriminator 3
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L538:
	.loc 1 193 0 discriminator 1
	cmp	r3, r1
	bne	.L539
.LVL491:
	.loc 1 198 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L543:
	.align	2
.L542:
	.word	g_nand_ops
	.word	g_nand_phy_info
.LFE276:
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	ftl_memset
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memset, %function
ftl_memset:
.LFB277:
	.loc 1 201 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL492:
	mov	ip, sp
.LCFI212:
	push	{r4, fp, ip, lr, pc}
.LCFI213:
	sub	fp, ip, #4
.LCFI214:
.LBB375:
	.loc 1 202 0
	cmp	r2, #0
.LBE375:
	.loc 1 201 0
	mov	r4, r0
.LBB376:
	.loc 1 202 0
	beq	.L545
.LBE376:
.LBB377:
.LBB378:
	bl	memset
.LVL493:
.L545:
.LBE378:
.LBE377:
	.loc 1 203 0 discriminator 8
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.LFE277:
	.size	ftl_memset, .-ftl_memset
	.align	2
	.global	FtlMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
.LFB156:
	.loc 3 279 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI215:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI216:
	sub	fp, ip, #4
.LCFI217:
	.loc 3 282 0
	ldr	r4, .L650
	.loc 3 297 0
	movw	r3, #65535
	.loc 3 301 0
	movw	r2, #3178
	.loc 3 282 0
	mov	r5, #0
	.loc 3 311 0
	mov	r6, #12
	.loc 3 297 0
	str	r3, [r4, #3784]
	.loc 3 301 0
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	.loc 3 302 0
	movw	r2, #3180
	strh	r3, [r4, r2]	@ movhi
	.loc 3 303 0
	mov	r2, #32
	movw	r3, #3124
	.loc 3 310 0
	ldrh	r0, [r4, #82]
	.loc 3 303 0
	strh	r2, [r4, r3]	@ movhi
	.loc 3 304 0
	mov	r2, #128
	movw	r3, #3126
	.loc 3 282 0
	str	r5, [r4, #2516]
	.loc 3 304 0
	strh	r2, [r4, r3]	@ movhi
	.loc 3 305 0
	movw	r3, #3132
	strh	r5, [r4, r3]	@ movhi
	.loc 3 306 0
	movw	r3, #3182
	strh	r5, [r4, r3]	@ movhi
	.loc 3 310 0
	lsl	r0, r0, #1
	.loc 3 307 0
	movw	r3, #3218
	.loc 3 283 0
	str	r5, [r4, #2520]
	.loc 3 307 0
	strh	r5, [r4, r3]	@ movhi
	.loc 3 312 0
	mov	r7, #20
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
	.loc 3 310 0
	bl	ftl_malloc
.LVL494:
	str	r0, [r4, #3164]
	.loc 3 311 0
	ldrh	r0, [r4, #82]
	mul	r0, r6, r0
	bl	ftl_malloc
.LVL495:
	.loc 3 312 0
	ldrh	r3, [r4, #8]
	.loc 3 311 0
	str	r0, [r4, #3172]
	.loc 3 312 0
	mul	r7, r7, r3
	.loc 3 313 0
	lsl	r8, r7, #2
	mov	r0, r8
	bl	ftl_malloc
.LVL496:
	str	r0, [r4, #3792]
	.loc 3 314 0
	mov	r0, r7
	bl	ftl_malloc
.LVL497:
	str	r0, [r4, #3796]
	.loc 3 315 0
	mov	r0, r8
	bl	ftl_malloc
.LVL498:
	str	r0, [r4, #3800]
	.loc 3 316 0
	mov	r0, r7
	bl	ftl_malloc
.LVL499:
	str	r0, [r4, #3760]
	.loc 3 317 0
	mov	r0, r7
	bl	ftl_malloc
.LVL500:
	.loc 3 320 0
	ldrh	r3, [r4, #8]
	.loc 3 319 0
	ldrh	r7, [r4, #86]
	.loc 3 317 0
	str	r0, [r4, #3156]
	.loc 3 320 0
	lsl	r3, r3, #1
	.loc 3 321 0
	mov	r0, r7
	.loc 3 320 0
	add	r3, r3, #1
	str	r3, [r4, #3160]
	.loc 3 321 0
	bl	ftl_malloc
.LVL501:
	str	r0, [r4, #3240]
	.loc 3 322 0
	mov	r0, r7
	bl	ftl_malloc
.LVL502:
	str	r0, [r4, #3804]
	.loc 3 323 0
	mov	r0, r7
	bl	ftl_malloc
.LVL503:
	str	r0, [r4, #3808]
	.loc 3 324 0
	ldr	r0, [r4, #3160]
	mul	r0, r0, r7
	bl	ftl_malloc
.LVL504:
	str	r0, [r4, #3148]
	.loc 3 325 0
	mov	r0, r7
	bl	ftl_malloc
.LVL505:
	str	r0, [r4, #3776]
	.loc 3 326 0
	mov	r0, r7
	bl	ftl_malloc
.LVL506:
	str	r0, [r4, #3768]
	.loc 3 327 0
	ldr	r0, [r4, #3160]
	mul	r0, r6, r0
	bl	ftl_malloc
.LVL507:
	.loc 3 329 0
	ldrh	r3, [r4, #88]
	ldrh	r7, [r4, #8]
	.loc 3 327 0
	str	r0, [r4, #3144]
	.loc 3 329 0
	mul	r7, r7, r3
	.loc 3 330 0
	mov	r0, r7
	bl	ftl_malloc
.LVL508:
	str	r0, [r4, #3244]
	.loc 3 331 0
	lsl	r0, r7, #2
	.loc 3 335 0
	movw	r7, #3812
	.loc 3 331 0
	bl	ftl_malloc
.LVL509:
	.loc 3 332 0
	ldrh	r3, [r4, #88]
	.loc 3 331 0
	str	r0, [r4, #3772]
	.loc 3 332 0
	ldr	r0, [r4, #3160]
	mul	r0, r0, r3
	bl	ftl_malloc
.LVL510:
	str	r0, [r4, #3152]
	.loc 3 335 0
	ldrh	r0, [r4, #18]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, r7]	@ movhi
	.loc 3 336 0
	bl	ftl_malloc
.LVL511:
	str	r0, [r4, #3816]
	.loc 3 337 0
	ldrh	r0, [r4, r7]
	.loc 3 338 0
	ldr	r3, .L650+4
	.loc 3 337 0
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r4, r7]	@ movhi
	.loc 3 338 0
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
.LVL512:
	.loc 3 345 0
	ldrh	r7, [r4, #18]
	.loc 3 338 0
	str	r0, [r4, #3820]
	.loc 3 339 0
	add	r0, r0, #32
	str	r0, [r4, #2304]
	.loc 3 345 0
	lsl	r7, r7, #1
	.loc 3 347 0
	mov	r0, r7
	bl	ftl_malloc
.LVL513:
	str	r0, [r4, #3824]
	.loc 3 349 0
	mov	r0, r7
	bl	ftl_malloc
.LVL514:
	.loc 3 350 0
	ldr	r7, [r4, #104]
	.loc 3 349 0
	str	r0, [r4, #2300]
	.loc 3 350 0
	lsl	r7, r7, #1
	.loc 3 351 0
	mov	r0, r7
	bl	ftl_malloc
.LVL515:
	str	r0, [r4, #3828]
	.loc 3 352 0
	mov	r0, r7
	bl	ftl_malloc
.LVL516:
	str	r0, [r4, #3832]
	.loc 3 353 0
	ldrh	r0, [r4, #18]
	lsr	r0, r0, #3
	add	r0, r0, #4
	bl	ftl_malloc
.LVL517:
	str	r0, [r4]
	.loc 3 354 0
	ldrh	r0, [r4, #96]
	lsl	r0, r0, #1
	bl	ftl_malloc
.LVL518:
	str	r0, [r4, #124]
	.loc 3 355 0
	ldrh	r0, [r4, #96]
	lsl	r0, r0, #1
	bl	ftl_malloc
.LVL519:
	str	r0, [r4, #3836]
	.loc 3 356 0
	ldrh	r0, [r4, #96]
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL520:
	str	r0, [r4, #3840]
	.loc 3 357 0
	ldrh	r0, [r4, #98]
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL521:
	.loc 3 358 0
	ldrh	r2, [r4, #98]
	mov	r1, r5
	.loc 3 357 0
	str	r0, [r4, #3844]
	.loc 3 370 0
	movw	r5, #3860
	.loc 3 358 0
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL522:
	.loc 3 360 0
	ldrh	r0, [r4, #112]
	.loc 3 361 0
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL523:
	str	r0, [r4, #3848]
	.loc 3 362 0
	ldr	r0, [r4, #104]
	.loc 3 363 0
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL524:
	str	r0, [r4, #3852]
	.loc 3 364 0
	ldrh	r0, [r4, #114]
	.loc 3 365 0
	mul	r0, r6, r0
	bl	ftl_malloc
.LVL525:
	.loc 3 366 0
	ldrh	r3, [r4, #114]
	.loc 3 365 0
	str	r0, [r4, #2468]
	.loc 3 366 0
	ldrh	r0, [r4, #86]
	.loc 3 367 0
	mul	r0, r0, r3
	bl	ftl_malloc
.LVL526:
	str	r0, [r4, #3856]
	.loc 3 368 0
	ldrh	r0, [r4, #18]
	.loc 3 369 0
	lsl	r0, r0, #3
	bl	ftl_malloc
.LVL527:
	str	r0, [r4, #2292]
	.loc 3 370 0
	ldrh	r0, [r4, #74]
	.loc 3 371 0
	ldrh	r3, [r4, #30]
	.loc 3 370 0
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r4, r5]	@ movhi
	.loc 3 371 0
	mul	r0, r0, r3
	.loc 3 372 0
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL528:
	.loc 3 374 0
	ldrh	r2, [r4, r5]
	add	ip, r4, #156
	.loc 3 373 0
	ldrh	lr, [r4, #30]
	mov	r3, #1
.LVL529:
	.loc 3 372 0
	str	r0, [r4, #156]
	lsl	r2, r2, #2
	.loc 3 373 0
	mov	r1, r2
.L550:
	.loc 3 373 0 is_stmt 0 discriminator 1
	cmp	r3, lr
	bcc	.L551
	ldr	r2, .L650+8
	.loc 3 379 0 is_stmt 1
	mov	r1, #0
	add	r3, r2, r3, lsl #2
.LVL530:
	add	r2, r2, #56
	add	r3, r3, #24
.L552:
	.loc 3 377 0 discriminator 1
	cmp	r2, r3
	bne	.L553
	.loc 3 383 0
	ldr	r3, [r4, #3828]
	cmp	r3, #0
	bne	.L554
.L556:
	.loc 3 385 0
	ldr	r1, .L650+12
	ldr	r0, .L650+16
	bl	printk
.LVL531:
	.loc 3 386 0
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL532:
.L551:
	.loc 3 374 0 discriminator 3
	ldr	r0, [r4, #156]
	.loc 3 373 0 discriminator 3
	add	r3, r3, #1
	.loc 3 374 0 discriminator 3
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L550
.LVL533:
.L553:
	.loc 3 379 0 discriminator 2
	str	r1, [r3, #4]!
	b	.L552
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
	ldr	r3, [r4, #2468]
	cmp	r3, #0
	beq	.L556
	.loc 3 409 0
	ldr	r3, [r4, #3856]
	cmp	r3, #0
	beq	.L556
	.loc 3 414 0
	ldr	r3, [r4, #2292]
	cmp	r3, #0
	beq	.L556
	.loc 3 419 0
	ldr	r3, [r4, #156]
	cmp	r3, #0
	beq	.L556
	.loc 3 425 0
	ldr	r3, [r4, #2300]
	cmp	r3, #0
	beq	.L556
	.loc 3 430 0
	ldr	r3, [r4, #3164]
	cmp	r3, #0
	beq	.L556
	.loc 3 430 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3172]
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
	ldr	r3, [r4, #3156]
	cmp	r3, #0
	beq	.L556
	.loc 3 435 0 discriminator 4
	ldr	r3, [r4, #3796]
	cmp	r3, #0
	beq	.L556
	.loc 3 440 0 is_stmt 1
	ldr	r3, [r4, #3240]
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
	ldr	r3, [r4, #3148]
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
	ldr	r3, [r4, #3144]
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
	ldr	r3, [r4, #3152]
	cmp	r3, #0
	beq	.L556
	.loc 3 452 0 is_stmt 1
	ldr	r3, [r4, #2304]
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
	ldr	r3, .L650
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
	mov	r0, #0
	.loc 3 484 0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L651:
	.align	2
.L650:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+128
	.word	.LANCHOR1+186
	.word	.LC81
.LFE156:
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
.LFB175:
	.loc 4 66 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL534:
	mov	ip, sp
.LCFI218:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
.LCFI219:
	sub	fp, ip, #4
.LCFI220:
	.loc 4 69 0
	ldr	r6, .L658
	movw	r3, #3860
	.loc 4 66 0
	mov	r4, r0
	.loc 4 74 0
	ldr	r8, .L658+4
	sub	r5, r4, #2
	.loc 4 69 0
	ldrh	r2, [r6, r3]
	add	r4, r4, #1020
	.loc 4 74 0
	ldr	r9, .L658+8
	.loc 4 66 0
	mov	r7, r1
	add	r4, r4, #2
	.loc 4 69 0
	mov	r1, #0
.LVL535:
	mov	r0, r7
.LVL536:
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL537:
.L655:
	.loc 4 72 0
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	.loc 4 74 0
	ldrh	r2, [r6, #74]
	cmp	r2, r3
	bhi	.L654
	.loc 4 74 0 is_stmt 0 discriminator 1
	mov	r2, #74
	mov	r1, r8
	mov	r0, r9
	bl	printk
.LVL538:
.L654:
	.loc 4 75 0 is_stmt 1 discriminator 2
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	.loc 4 70 0 discriminator 2
	cmp	r4, r5
	.loc 4 75 0 discriminator 2
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r7, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r7, r1, lsl #2]
	.loc 4 70 0 discriminator 2
	bne	.L655
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L659:
	.align	2
.L658:
	.word	.LANCHOR0
	.word	.LANCHOR1+197
	.word	.LC1
.LFE175:
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	load_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
.LFB203:
	.loc 2 488 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL539:
	mov	ip, sp
.LCFI221:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI222:
	sub	fp, ip, #4
.LCFI223:
	.loc 2 492 0
	ldr	r4, .L666
	.loc 2 488 0
	uxth	r6, r0
.LVL540:
	uxth	r8, r1
.LVL541:
	.loc 2 492 0
	ldrh	r3, [r4, #112]
	cmp	r3, r6
	bcs	.L661
	.loc 2 492 0 is_stmt 0 discriminator 1
	mov	r2, #492
	ldr	r1, .L666+4
	ldr	r0, .L666+8
	bl	printk
.LVL542:
.L661:
	.loc 2 493 0 is_stmt 1
	ldr	r3, [r4, #3848]
	mov	r5, #12
	ldr	r7, [r3, r6, lsl #2]
.LVL543:
	.loc 2 495 0
	cmp	r7, #0
	bne	.L662
	.loc 2 496 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2468]
	ldrh	r2, [r4, #86]
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL544:
	.loc 2 497 0
	ldr	r3, [r4, #2468]
	strh	r6, [r3, r5]	@ movhi
	.loc 2 498 0
	ldr	r3, [r4, #2468]
	add	r5, r3, r5
	str	r7, [r5, #4]
.LVL545:
.L663:
	.loc 2 519 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL546:
.L662:
	.loc 2 502 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2468]
	.loc 2 504 0
	mov	r2, #1
	ldr	r0, .L666+12
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
.LVL547:
	.loc 2 505 0
	ldr	r8, [r4, #3232]
.LVL548:
	.loc 2 507 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L664
	.loc 2 508 0
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L666+16
	bl	printk
.LVL549:
	.loc 2 509 0
	mov	r3, #4
	ldr	r1, [r4, #3232]
	mov	r2, r3
	ldr	r0, .L666+20
	bl	rknand_print_hex
.LVL550:
	.loc 2 510 0
	ldrh	r3, [r4, #112]
	mov	r2, #4
	ldr	r1, [r4, #3848]
	ldr	r0, .L666+24
	bl	rknand_print_hex
.LVL551:
.L664:
	.loc 2 513 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L665
	.loc 2 513 0 is_stmt 0 discriminator 1
	movw	r2, #513
	ldr	r1, .L666+4
	ldr	r0, .L666+8
	bl	printk
.LVL552:
.L665:
	.loc 2 516 0 is_stmt 1
	ldr	r3, [r4, #2468]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	.loc 2 517 0
	strh	r6, [r3, r5]	@ movhi
	.loc 2 518 0
	b	.L663
.L667:
	.align	2
.L666:
	.word	.LANCHOR0
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0+3220
	.word	.LC82
	.word	.LC83
	.word	.LC84
.LFE203:
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
.LFB204:
	.loc 2 522 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL553:
	mov	ip, sp
.LCFI224:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI225:
	sub	fp, ip, #4
.LCFI226:
	sub	sp, sp, #8
	.loc 2 531 0
	mov	r1, #0
	ldrh	r2, [r0, #10]
	.loc 2 522 0
	mov	r4, r0
	.loc 2 525 0
	ldr	r5, [r0, #20]
.LVL554:
	.loc 2 523 0
	ldr	r9, [r0, #12]
.LVL555:
	.loc 2 524 0
	ldr	r6, [r0, #24]
.LVL556:
	.loc 2 531 0
	lsl	r2, r2, #1
	mov	r0, r5
.LVL557:
	bl	ftl_memset
.LVL558:
	.loc 2 532 0
	mov	r2, #0
.LVL559:
.L669:
	.loc 2 532 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L673
	.loc 2 540 0 is_stmt 1
	ldrh	r2, [r5]
.LVL560:
	.loc 2 543 0
	mov	r7, #0
	.loc 2 544 0
	ldr	ip, .L682
	.loc 2 541 0
	mov	r10, r7
.LVL561:
.L674:
	.loc 2 543 0 discriminator 1
	ldrh	r1, [r4, #10]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L678
	.loc 2 559 0
	mov	r0, r10
.LVL562:
	sub	sp, fp, #40
.LCFI227:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL563:
.L673:
.LCFI228:
	.loc 2 533 0
	uxth	r3, r2
	.loc 2 534 0
	mov	r1, #0
	.loc 2 533 0
	ldr	r0, [r6, r3, lsl #2]
.LVL564:
	ubfx	r0, r0, #10, #16
.LVL565:
.L670:
	.loc 2 534 0 discriminator 1
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L669
.L672:
	.loc 2 535 0
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r9, r3]
	cmp	ip, r0
	.loc 2 536 0
	ldrheq	ip, [r5, r3]
	addeq	ip, ip, #1
	strheq	ip, [r5, r3]	@ movhi
	b	.L670
.LVL566:
.L678:
	.loc 2 544 0
	ldrh	r1, [r4]
	uxth	r6, r7
.LVL567:
	cmp	r1, r3
	bne	.L675
	.loc 2 544 0 is_stmt 0 discriminator 1
	ldrh	r0, [r4, #2]
.LVL568:
	ldrh	r1, [ip, #80]
	cmp	r0, r1
	.loc 2 545 0 is_stmt 1 discriminator 1
	lslcc	r0, r6, #1
	strhcc	r1, [r5, r0]	@ movhi
.LVL569:
.L675:
	.loc 2 546 0
	lsl	r6, r6, #1
	ldrh	r8, [r5, r6]
	cmp	r2, r8
	movhi	r10, r3
	movhi	r2, r8
	.loc 2 550 0
	cmp	r8, #0
	bne	.L677
	.loc 2 550 0 is_stmt 0 discriminator 1
	ldrh	r0, [r9, r6]
.LVL570:
	cmp	r0, #0
	beq	.L677
	.loc 2 552 0 is_stmt 1
	mov	r1, #1
	str	ip, [fp, #-48]
	str	r2, [fp, #-44]
	bl	FtlFreeSysBlkQueueIn
.LVL571:
	.loc 2 553 0
	strh	r8, [r9, r6]	@ movhi
	.loc 2 554 0
	ldr	ip, [fp, #-48]
	ldrh	r3, [r4, #8]
	ldr	r2, [fp, #-44]
.LVL572:
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.LVL573:
.L677:
	add	r7, r7, #1
	b	.L674
.L683:
	.align	2
.L682:
	.word	.LANCHOR0
.LFE204:
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
.LFB207:
	.loc 2 632 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL574:
	mov	ip, sp
.LCFI229:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI230:
	sub	fp, ip, #4
.LCFI231:
	.loc 2 640 0
	ldrh	r3, [r0]
	movw	r2, #65535
	.loc 2 632 0
	mov	r4, r0
	.loc 2 635 0
	ldr	r5, [r0, #12]
.LVL575:
	.loc 2 640 0
	cmp	r3, r2
	bne	.L685
	.loc 2 641 0
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L686
	.loc 2 641 0 is_stmt 0 discriminator 1
	movw	r2, #641
	ldr	r1, .L694
	ldr	r0, .L694+4
	bl	printk
.LVL576:
.L686:
	.loc 2 642 0 is_stmt 1
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 643 0
	bl	FtlFreeSysBlkQueueOut
.LVL577:
	.loc 2 644 0
	mov	r3, #0
	.loc 2 643 0
	strh	r0, [r5]	@ movhi
	.loc 2 644 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 645 0
	strh	r3, [r4]	@ movhi
	.loc 2 646 0
	ldr	r3, [r4, #28]
	add	r3, r3, #1
	str	r3, [r4, #28]
.LVL578:
.L687:
	.loc 2 677 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL579:
.L685:
	.loc 2 650 0
	lsl	r3, r3, #1
	.loc 2 656 0
	ldr	r2, [r0, #28]
	.loc 2 636 0
	ldr	r6, [r0, #24]
.LVL580:
	.loc 2 661 0
	mov	r1, #255
	.loc 2 650 0
	ldrh	r7, [r5, r3]
	.loc 2 651 0
	ldrh	r3, [r0, #2]
	ldr	r5, .L694+8
.LVL581:
	orr	r3, r3, r7, lsl #10
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
	ldr	r2, .L694+12
	strh	r2, [r3, #8]	@ movhi
	.loc 2 658 0
	ldrh	r2, [r0, #4]
	.loc 2 659 0
	strh	r7, [r3, #2]	@ movhi
	.loc 2 658 0
	strh	r2, [r3]	@ movhi
	.loc 2 661 0
	ldrh	r2, [r5, #80]
	ldr	r0, [r5, #3240]
.LVL582:
	lsl	r2, r2, #3
	bl	ftl_memset
.LVL583:
	.loc 2 663 0
	mov	r2, #0
	.loc 2 662 0
	mov	r3, r2
.LVL584:
.L688:
	.loc 2 663 0 discriminator 1
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L690
	.loc 2 670 0
	mov	r2, #1
	mov	r3, #0
.LVL585:
	mov	r1, r2
	ldr	r0, .L694+16
	bl	FlashProgPages
.LVL586:
	.loc 2 671 0
	ldrh	r3, [r4, #2]
	.loc 2 675 0
	mov	r0, r4
	.loc 2 671 0
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	.loc 2 675 0
	bl	ftl_map_blk_gc
.LVL587:
	.loc 2 676 0
	b	.L687
.LVL588:
.L690:
	uxth	r1, r2
	.loc 2 664 0
	ldr	r0, [r6, r1, lsl #2]
	cmp	r7, r0, lsr #10
	bne	.L689
	.loc 2 666 0
	ldr	r0, [r5, #3240]
	.loc 2 665 0
	add	r3, r3, #1
.LVL589:
	uxth	r3, r3
.LVL590:
	.loc 2 666 0
	str	r1, [r0, r3, lsl #3]
	.loc 2 667 0
	ldr	r0, [r6, r1, lsl #2]
	ldr	r1, [r5, #3240]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L689:
	add	r2, r2, #1
	b	.L688
.L695:
	.align	2
.L694:
	.word	.LANCHOR1+227
	.word	.LC1
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3220
.LFE207:
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
.LFB208:
	.loc 2 680 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL591:
	mov	ip, sp
.LCFI232:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI233:
	sub	fp, ip, #4
.LCFI234:
	.loc 2 686 0
	ldr	r7, .L715
	.loc 2 680 0
	mov	r4, r0
	mov	r8, r1
	mov	r9, r2
	.loc 2 683 0
	mov	r6, #0
.LVL592:
	mov	r5, r7
.L697:
	.loc 2 686 0
	ldr	r3, [r7, #2500]
	add	r3, r3, #1
	str	r3, [r7, #2500]
	.loc 2 687 0
	ldrh	r3, [r7, #80]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L698
	.loc 2 687 0 is_stmt 0 discriminator 1
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L699
.L698:
	.loc 2 688 0 is_stmt 1
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.LVL593:
.L699:
	.loc 2 699 0
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L700
	.loc 2 699 0 is_stmt 0 discriminator 1
	movw	r2, #699
	ldr	r1, .L715+4
	ldr	r0, .L715+8
	bl	printk
.LVL594:
.L700:
	.loc 2 700 0 is_stmt 1
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L701
	.loc 2 700 0 is_stmt 0 discriminator 1
	mov	r2, #700
	ldr	r1, .L715+4
	ldr	r0, .L715+8
	bl	printk
.LVL595:
.L701:
	.loc 2 701 0 is_stmt 1
	ldrh	r3, [r4]
.LBB384:
.LBB385:
.LBB386:
	.loc 1 202 0
	mov	r1, #16
.LBE386:
.LBE385:
.LBE384:
	.loc 2 701 0
	ldr	r2, [r4, #12]
	.loc 2 705 0
	ldr	r0, [r5, #3244]
	.loc 2 701 0
	lsl	r3, r3, #1
	ldrh	r10, [r2, r3]
	.loc 2 703 0
	ldrh	r3, [r4, #2]
	.loc 2 705 0
	str	r0, [r5, #3232]
	.loc 2 704 0
	str	r9, [r5, #3228]
	.loc 2 703 0
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #3224]
.LBB389:
.LBB388:
.LBB387:
	.loc 1 202 0
	bl	__memzero
.LVL596:
.LBE387:
.LBE388:
.LBE389:
	.loc 2 708 0
	ldr	r3, [r5, #3232]
.LVL597:
	.loc 2 709 0
	ldr	r2, [r4, #28]
	.loc 2 713 0
	ldr	r0, .L715+12
	.loc 2 710 0
	strh	r8, [r3, #8]	@ movhi
	.loc 2 709 0
	str	r2, [r3, #4]
	.loc 2 711 0
	ldrh	r2, [r4, #4]
	.loc 2 712 0
	strh	r10, [r3, #2]	@ movhi
	.loc 2 711 0
	strh	r2, [r3]	@ movhi
	.loc 2 713 0
	mov	r3, #1
.LVL598:
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL599:
	.loc 2 714 0
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	.loc 2 716 0
	ldr	r2, [r5, #3220]
	cmn	r2, #1
	bne	.L702
	.loc 2 717 0
	ldr	r1, [r5, #3224]
	.loc 2 718 0
	add	r6, r6, #1
.LVL600:
	.loc 2 717 0
	ldr	r0, .L715+16
	.loc 2 718 0
	uxth	r6, r6
.LVL601:
	.loc 2 717 0
	bl	printk
.LVL602:
	.loc 2 719 0
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	.loc 2 720 0
	ldrhls	r3, [r5, #80]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	.loc 2 722 0
	cmp	r6, #3
	bls	.L704
	.loc 2 723 0
	mov	r2, r6
	ldr	r1, [r5, #3224]
	ldr	r0, .L715+20
	bl	printk
.LVL603:
.L705:
	b	.L705
.L704:
	.loc 2 727 0
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L697
.L714:
	b	.L714
.L702:
	.loc 2 731 0
	cmp	r3, #1
	beq	.L697
	.loc 2 734 0
	ldr	r2, [r5, #3224]
	.loc 2 736 0
	mov	r0, #0
	.loc 2 734 0
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	.loc 2 736 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L716:
	.align	2
.L715:
	.word	.LANCHOR0
	.word	.LANCHOR1+258
	.word	.LC1
	.word	.LANCHOR0+3220
	.word	.LC85
	.word	.LC86
.LFE208:
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
.LFB206:
	.loc 2 586 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL604:
	mov	ip, sp
.LCFI235:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI236:
	sub	fp, ip, #4
.LCFI237:
	sub	sp, sp, #8
	.loc 2 586 0
	mov	r4, r0
	.loc 2 590 0
	ldr	r6, [r0, #12]
.LVL605:
	.loc 2 591 0
	ldr	r9, [r0, #24]
.LVL606:
	.loc 2 594 0
	bl	ftl_free_no_use_map_blk
.LVL607:
	.loc 2 596 0
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L729
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L718
	.loc 2 597 0
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r6, r0]
.LVL608:
	.loc 2 598 0
	cmp	r8, #0
	beq	.L718
	.loc 2 598 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L718
	.loc 2 599 0 is_stmt 1
	mov	r2, #1
	str	r2, [r4, #32]
	.loc 2 600 0
	strh	r3, [r6, r0]	@ movhi
	.loc 2 601 0
	ldrh	r3, [r4, #8]
	.loc 2 602 0
	ldrh	r2, [r4, #2]
	.loc 2 601 0
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 602 0
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L719
	.loc 2 603 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL609:
.L719:
	.loc 2 613 0 discriminator 1
	ldr	r10, .L729+4
	.loc 2 586 0 discriminator 1
	mov	r6, #0
.LVL610:
.L720:
	.loc 2 605 0 discriminator 1
	ldrh	r2, [r4, #6]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L724
	.loc 2 621 0
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.LVL611:
	.loc 2 622 0
	mov	r3, #0
	str	r3, [r4, #32]
.LVL612:
.L718:
	.loc 2 626 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L725
	.loc 2 627 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL613:
.L725:
	.loc 2 629 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI238:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL614:
.L724:
.LCFI239:
	uxth	r7, r6
	.loc 2 606 0
	ldr	r2, [r9, r7, lsl #2]
	cmp	r8, r2, lsr #10
	bne	.L721
	.loc 2 608 0
	ldr	ip, [r5, #3244]
	.loc 2 607 0
	ldr	r2, [r5, #3804]
	.loc 2 611 0
	ldr	r0, .L729+8
	.loc 2 608 0
	str	ip, [r5, #3232]
	.loc 2 607 0
	str	r2, [r5, #3228]
	.loc 2 609 0
	ldr	r2, [r9, r7, lsl #2]
	str	r3, [fp, #-48]
	.loc 2 608 0
	str	ip, [fp, #-44]
	.loc 2 609 0
	str	r2, [r5, #3224]
	.loc 2 611 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL615:
	.loc 2 613 0
	ldr	ip, [fp, #-44]
	ldr	r3, [fp, #-48]
.LVL616:
	ldrh	r2, [ip, #8]
	cmp	r2, r3
	beq	.L722
	.loc 2 613 0 is_stmt 0 discriminator 1
	movw	r2, #613
	mov	r1, r10
	ldr	r0, .L729+12
	bl	printk
.LVL617:
.L722:
	.loc 2 614 0 is_stmt 1
	ldr	r3, [r5, #3220]
.LVL618:
	cmn	r3, #1
	.loc 2 615 0
	moveq	r3, #0
	streq	r3, [r9, r7, lsl #2]
	.loc 2 614 0
	beq	.L721
	.loc 2 617 0
	ldr	r2, [r5, #3228]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.LVL619:
.L721:
	add	r6, r6, #1
	b	.L720
.L730:
	.align	2
.L729:
	.word	.LANCHOR0
	.word	.LANCHOR1+274
	.word	.LANCHOR0+3220
	.word	.LC1
.LFE206:
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	flush_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
.LFB209:
	.loc 2 739 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL620:
	mov	ip, sp
.LCFI240:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI241:
	sub	fp, ip, #4
.LCFI242:
	.loc 2 742 0
	ldr	r5, .L732
	.loc 2 739 0
	uxth	r0, r0
	.loc 2 742 0
	mov	r4, #12
	mul	r4, r4, r0
	ldr	r3, [r5, #2468]
	.loc 2 743 0
	add	r0, r5, #3856
.LVL621:
	add	r0, r0, #8
	.loc 2 742 0
	add	r2, r3, r4
	.loc 2 743 0
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
.LVL622:
	.loc 2 744 0
	ldr	r0, [r5, #2468]
	add	r4, r0, r4
	.loc 2 746 0
	mov	r0, #0
	.loc 2 744 0
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	.loc 2 746 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L733:
	.align	2
.L732:
	.word	.LANCHOR0
.LFE209:
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
.LFB212:
	.loc 2 802 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL623:
	mov	ip, sp
.LCFI243:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI244:
	sub	fp, ip, #4
.LCFI245:
	sub	sp, sp, #4
	.loc 2 806 0
	mvn	r6, #0
	.loc 2 805 0
	ldr	r4, .L748
	.loc 2 802 0
	mov	r7, r1
	mov	r10, r2
	.loc 2 805 0
	ldrh	r3, [r4, #84]
	add	r3, r3, #7
	lsr	r9, r0, r3
	.loc 2 806 0
	bic	r6, r0, r6, lsl r3
	.loc 2 808 0
	ldr	r3, [r4, #2480]
	.loc 2 805 0
	uxth	r8, r9
.LVL624:
	.loc 2 806 0
	uxth	r6, r6
.LVL625:
	.loc 2 808 0
	cmp	r0, r3
	bcc	.L735
	.loc 2 808 0 is_stmt 0 discriminator 1
	mov	r2, #808
	ldr	r1, .L748+4
	ldr	r0, .L748+8
.LVL626:
	bl	printk
.LVL627:
.L735:
	.loc 2 810 0 is_stmt 1
	ldrh	r1, [r4, #114]
	mov	r2, #0
	.loc 2 811 0
	ldr	r0, [r4, #2468]
.LVL628:
	mov	r3, #12
.LVL629:
.L736:
	uxth	r5, r2
	.loc 2 810 0 discriminator 1
	cmp	r5, r1
	bcc	.L741
	str	r3, [fp, #-44]
	.loc 2 825 0
	bl	select_l2p_ram_region
.LVL630:
	.loc 2 826 0
	ldr	r3, [fp, #-44]
	.loc 2 825 0
	mov	r5, r0
	.loc 2 826 0
	ldr	r2, [r4, #2468]
	mul	r3, r3, r0
	add	r1, r2, r3
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L742
	.loc 2 826 0 is_stmt 0 discriminator 1
	ldr	r3, [r1, #4]
	cmp	r3, #0
	bge	.L742
	.loc 2 827 0 is_stmt 1
	bl	flush_l2p_region
.LVL631:
.L742:
	.loc 2 830 0
	mov	r1, r5
	uxth	r0, r9
	bl	load_l2p_region
.LVL632:
	.loc 2 831 0
	b	.L737
.L741:
	add	r2, r2, #1
	.loc 2 811 0
	mla	ip, r3, r2, r0
	ldrh	ip, [ip, #-12]
	cmp	ip, r8
	bne	.L736
.L737:
	.loc 2 813 0
	cmp	r10, #0
	mov	r3, #12
	bne	.L738
	.loc 2 814 0
	ldr	r2, [r4, #2468]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r7]
.L739:
	.loc 2 820 0
	ldr	r2, [r4, #2468]
	mov	r3, #12
	.loc 2 832 0
	mov	r0, #0
	.loc 2 820 0
	mla	r5, r3, r5, r2
.LVL633:
	ldr	r3, [r5, #4]
	cmn	r3, #1
	.loc 2 821 0
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	.loc 2 832 0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL634:
.L738:
	.loc 2 816 0
	mul	r3, r3, r5
	ldr	r2, [r4, #2468]
	ldr	r1, [r7]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r6, lsl #2]
	.loc 2 817 0
	ldr	r2, [r4, #2468]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	.loc 2 818 0
	movw	r3, #2472
	strh	r8, [r4, r3]	@ movhi
	b	.L739
.L749:
	.align	2
.L748:
	.word	.LANCHOR0
	.word	.LANCHOR1+289
	.word	.LC1
.LFE212:
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
.LFB230:
	.loc 2 1677 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL635:
	mov	ip, sp
.LCFI246:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI247:
	sub	fp, ip, #4
.LCFI248:
	sub	sp, sp, #4
	.loc 2 1677 0
	mov	r7, r0
	.loc 2 1680 0
	ldr	r5, .L760
	.loc 2 1678 0
	ubfx	r0, r1, #10, #16
.LVL636:
	.loc 2 1677 0
	str	r1, [fp, #-36]
	.loc 2 1678 0
	bl	P2V_block_in_plane
.LVL637:
	.loc 2 1680 0
	ldr	r2, [r5, #2300]
.LVL638:
	lsl	r6, r0, #1
	ldrh	r3, [r2, r6]
	cmp	r3, #0
	bne	.L751
	.loc 2 1681 0
	ldr	r4, [r5, #2316]
	cmp	r4, #0
	beq	.L752
.LBB390:
	.loc 2 1685 0
	ldr	r2, [r5, #2292]
	.loc 2 1684 0
	add	r8, r5, #2320
	ldrh	r1, [r8]
.LVL639:
	.loc 2 1693 0
	movw	lr, #65535
	.loc 2 1685 0
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.LVL640:
.L753:
	.loc 2 1686 0 discriminator 1
	uxth	ip, r3
	cmp	r1, ip
	bls	.L752
	.loc 2 1687 0
	cmp	r4, r0
	bne	.L754
	.loc 2 1688 0
	mov	r1, r4
.LVL641:
	ldr	r0, .L760+4
.LVL642:
	bl	List_remove_node
.LVL643:
	ldrh	r3, [r8]
	cmp	r3, #0
	bne	.L755
	.loc 2 1688 0 is_stmt 0 discriminator 1
	movw	r2, #1688
	ldr	r1, .L760+8
	ldr	r0, .L760+12
	bl	printk
.LVL644:
.L755:
	.loc 2 1688 0 discriminator 3
	ldrh	r3, [r8]
	.loc 2 1689 0 is_stmt 1 discriminator 3
	mov	r0, r4
	.loc 2 1688 0 discriminator 3
	sub	r3, r3, #1
	strh	r3, [r8]	@ movhi
	.loc 2 1689 0 discriminator 3
	bl	INSERT_DATA_LIST
.LVL645:
	.loc 2 1690 0 discriminator 3
	ldr	r2, [r5, #2300]
	ldrh	r3, [r2, r6]
.LVL646:
.L751:
.LBE390:
	.loc 2 1699 0
	add	r3, r3, #1
	strh	r3, [r2, r6]	@ movhi
	b	.L752
.LVL647:
.L754:
.LBB391:
	.loc 2 1693 0
	lsl	r4, r4, #3
.LVL648:
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
.LVL649:
	cmp	r4, lr
	bne	.L753
.L752:
.LBE391:
	.loc 2 1701 0
	mov	r2, #1
	sub	r1, fp, #36
.LVL650:
	mov	r0, r7
.LVL651:
	bl	log2phys
.LVL652:
	ldmib	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L761:
	.align	2
.L760:
	.word	.LANCHOR0
	.word	.LANCHOR0+2316
	.word	.LANCHOR1+298
	.word	.LC1
.LFE230:
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
.LFB241:
	.loc 2 2325 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI249:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI250:
	sub	fp, ip, #4
.LCFI251:
	sub	sp, sp, #24
	.loc 2 2331 0
	mov	r5, #0
.LVL653:
	ldr	r6, .L770
	.loc 2 2330 0
	mov	r1, #0
	ldr	r0, .L770+4
	bl	printk
.LVL654:
	mov	r4, r6
.LVL655:
.L763:
	.loc 2 2331 0 discriminator 1
	ldr	r3, [r6, #2480]
	cmp	r5, r3
	bcc	.L769
	.loc 2 2353 0
	sub	sp, fp, #28
.LCFI252:
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL656:
.L769:
.LCFI253:
	.loc 2 2332 0
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
.LVL657:
	.loc 2 2333 0
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L764
	.loc 2 2334 0
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L770+8
	bl	printk
.LVL658:
.L764:
	.loc 2 2335 0
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L766
	.loc 2 2336 0
	str	r3, [r4, #3224]
	.loc 2 2340 0
	mov	r2, #0
	.loc 2 2338 0
	ldr	r3, [r4, #3240]
	.loc 2 2342 0
	mov	r1, #1
	.loc 2 2339 0
	ldr	r7, [r4, #3244]
	.loc 2 2342 0
	ldr	r0, .L770+12
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
.LVL659:
	.loc 2 2343 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L767
	.loc 2 2344 0
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L766
.L767:
	.loc 2 2347 0
	ldr	r2, [r4, #3228]
	ldr	r3, [r4, #3232]
	ldr	r0, .L770+16
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
.LVL660:
.L766:
	.loc 2 2331 0 discriminator 2
	add	r5, r5, #1
	b	.L763
.L771:
	.align	2
.L770:
	.word	.LANCHOR0
	.word	.LC87
	.word	.LC88
	.word	.LANCHOR0+3220
	.word	.LC89
.LFE241:
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
.LFB268:
	.loc 5 419 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI254:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI255:
	sub	fp, ip, #4
.LCFI256:
	sub	sp, sp, #88
	.loc 5 425 0
	ldr	r3, .L785
	ldr	r0, [r3, #2692]
	mov	r4, r3
	cmp	r0, #0
	beq	.L773
	.loc 5 426 0
	ldr	r1, [r3, #2696]
	ldr	r2, [r3, #2480]
	cmp	r1, r2
	bcs	.L774
	mov	r5, #2048
.LVL661:
.L779:
.LBB397:
.LBB398:
	.loc 5 428 0
	ldr	r0, [r4, #2696]
	ldr	r3, [r4, #2480]
	cmp	r0, r3
	bcs	.L776
	.loc 5 430 0
	mov	r2, #0
	sub	r1, fp, #108
	bl	log2phys
.LVL662:
	.loc 5 432 0
	ldr	r2, [fp, #-108]
	.loc 5 431 0
	ldr	r3, [r4, #2696]
	.loc 5 432 0
	cmn	r2, #1
	.loc 5 431 0
	add	r3, r3, #1
	str	r3, [r4, #2696]
	.loc 5 432 0
	beq	.L777
.LBB399:
	.loc 5 435 0
	str	r3, [fp, #-88]
	.loc 5 438 0
	sub	r0, fp, #20
	.loc 5 436 0
	ldr	r3, .L785+4
	.loc 5 439 0
	mov	r1, #1
	.loc 5 434 0
	str	r2, [fp, #-100]
	.loc 5 438 0
	mov	r2, #0
	str	r2, [r0, #-84]!
	.loc 5 436 0
	str	r3, [fp, #-96]
	.loc 5 437 0
	sub	r3, fp, #84
	str	r3, [fp, #-92]
	.loc 5 439 0
	bl	FlashReadPages
.LVL663:
	.loc 5 440 0
	ldr	r3, [fp, #-104]
	cmp	r3, #256
	bne	.L776
	.loc 5 442 0
	ldr	r0, [fp, #-108]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL664:
	bl	FtlGcRefreshBlock
.LVL665:
.L776:
.LBE399:
	mvn	r0, #0
.L772:
.LBE398:
.LBE397:
	.loc 5 466 0
	sub	sp, fp, #20
.LCFI257:
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L777:
.LCFI258:
.LBB401:
.LBB400:
	.loc 5 427 0
	subs	r5, r5, #1
	bne	.L779
	b	.L776
.LVL666:
.L774:
.LBE400:
.LBE401:
	.loc 5 451 0
	ldr	r2, [r3, #2484]
	.loc 5 449 0
	mov	r0, #0
	str	r0, [r3, #2692]
	.loc 5 450 0
	str	r0, [r3, #2696]
	.loc 5 451 0
	str	r2, [r3, #2688]
	b	.L772
.L773:
.LBB402:
	.loc 5 456 0
	ldr	r1, [r3, #2484]
	ldr	r2, [r3, #2688]
	add	ip, r1, #1048576
	cmp	r2, ip
	bhi	.L782
	.loc 5 454 0
	ldr	r3, [r3, #2540]
	.loc 5 457 0
	mov	ip, #33554432
	.loc 5 454 0
	lsr	r3, r3, #10
	.loc 5 457 0
	add	r2, r2, ip, asr r3
	cmp	r1, r2
	bhi	.L782
	.loc 5 458 0
	ldr	r3, .L785+8
	ldrb	r3, [r3, #28]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L772
.L782:
	.loc 5 460 0
	mov	r3, #1
	.loc 5 461 0
	mov	r0, #0
	.loc 5 460 0
	str	r3, [r4, #2692]
	.loc 5 461 0
	str	r0, [r4, #2696]
	.loc 5 462 0
	str	r1, [r4, #2688]
	b	.L772
.L786:
	.align	2
.L785:
	.word	.LANCHOR0
	.word	ftl_temp_buf
	.word	.LANCHOR0+2244
.LBE402:
.LFE268:
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_check_vpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
.LFB240:
	.loc 2 2273 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI259:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI260:
	sub	fp, ip, #4
.LCFI261:
	sub	sp, sp, #4
	.loc 2 2282 0
	mov	r4, #0
.LVL667:
	ldr	r6, .L811
	.loc 2 2279 0
	ldr	r1, .L811+4
	ldr	r0, .L811+8
	mov	r5, r6
	bl	printk
.LVL668:
.LBB409:
.LBB410:
.LBB411:
	.loc 1 202 0
	mov	r1, #8192
	ldr	r0, .L811+12
	bl	__memzero
.LVL669:
.L788:
.LBE411:
.LBE410:
.LBE409:
	.loc 2 2282 0 discriminator 1
	ldr	r3, [r6, #2480]
	cmp	r4, r3
	bcc	.L790
	.loc 2 2295 0 discriminator 1
	ldr	r8, .L811+12
	mov	r4, #0
.LVL670:
	.loc 2 2297 0 discriminator 1
	ldr	r9, .L811+16
	mov	r6, r4
.LVL671:
.L791:
	.loc 2 2293 0 discriminator 1
	ldrh	r2, [r5, #16]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L793
	.loc 2 2305 0
	ldr	r4, [r5, #2316]
	cmp	r4, #0
	beq	.L794
.LBB412:
	.loc 2 2307 0
	ldr	r3, .L811+20
	.loc 2 2309 0
	mov	r7, #0
	.loc 2 2313 0
	ldr	r9, .L811+12
	ldr	r10, .L811+24
	.loc 2 2307 0
	ldrh	r8, [r3]
.LVL672:
	.loc 2 2308 0
	ldr	r3, [r5, #2292]
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L795:
	.loc 2 2309 0 discriminator 1
	uxth	r3, r7
	cmp	r8, r3
	bls	.L794
	.loc 2 2311 0
	ldr	r2, [r5, #2300]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L796
	.loc 2 2314 0
	mov	r6, #1
	.loc 2 2313 0
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.LVL673:
.L796:
	.loc 2 2316 0
	ldr	r3, [r5, #2292]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L795
.L794:
.LBE412:
	.loc 2 2321 0
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	.loc 2 2321 0 is_stmt 0 discriminator 1
	movw	r2, #2321
	ldr	r1, .L811+4
	ldr	r0, .L811+28
	bl	printk
.LVL674:
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL675:
.L790:
	.loc 2 2284 0 is_stmt 1
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
.LVL676:
	.loc 2 2285 0
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L789
	.loc 2 2287 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL677:
	.loc 2 2288 0
	ldr	r2, .L811+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L789:
	.loc 2 2282 0 discriminator 2
	add	r4, r4, #1
	b	.L788
.LVL678:
.L793:
	.loc 2 2295 0
	uxth	r1, r4
	ldr	r3, [r5, #2300]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L792
	.loc 2 2297 0
	mov	r0, r9
	bl	printk
.LVL679:
	.loc 2 2298 0
	ldr	r3, [r5, #2300]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L792
	.loc 2 2298 0 is_stmt 0 discriminator 1
	ldrh	r2, [r8, r7]
	.loc 2 2300 0 is_stmt 1 discriminator 1
	cmp	r2, r3
	movhi	r6, #1
.L792:
	add	r4, r4, #1
	b	.L791
.L812:
	.align	2
.L811:
	.word	.LANCHOR0
	.word	.LANCHOR1+314
	.word	.LC90
	.word	check_vpc_table
	.word	.LC91
	.word	.LANCHOR0+2320
	.word	.LC92
	.word	.LC1
.LFE240:
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
.LFB217:
	.loc 2 946 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL680:
	mov	ip, sp
.LCFI262:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI263:
	sub	fp, ip, #4
.LCFI264:
	.loc 2 952 0
	ldr	r3, [r0, #36]
	.loc 2 946 0
	mov	r6, r0
	.loc 2 952 0
	cmp	r3, #0
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	.loc 2 954 0
	mov	r3, #0
	.loc 2 948 0
	ldrh	r5, [r0, #6]
	.loc 2 954 0
	str	r3, [r0, #36]
	.loc 2 956 0
	ldr	r3, .L820
	.loc 2 949 0
	ldr	r2, [r0, #24]
.LVL681:
	.loc 2 948 0
	sub	r5, r5, #1
	.loc 2 979 0
	uxth	r5, r5
	.loc 2 956 0
	ldr	r0, [r3, #3804]
.LVL682:
	mov	r4, r3
	.loc 2 957 0
	ldr	r1, [r3, #3244]
	.loc 2 956 0
	str	r0, [r3, #3228]
	.loc 2 957 0
	str	r1, [r3, #3232]
	.loc 2 979 0
	ldr	r2, [r2, r5, lsl #2]
.LVL683:
	.loc 2 981 0
	cmp	r2, #0
	.loc 2 980 0
	str	r2, [r3, #3224]
	.loc 2 981 0
	beq	.L815
	.loc 2 983 0
	mov	r2, #1
.LVL684:
	ldr	r0, .L820+4
	mov	r1, r2
	bl	FlashReadPages
.LVL685:
.L816:
	.loc 2 990 0
	ldr	r2, [r4, #3228]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
.LVL686:
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.LVL687:
.L815:
	.loc 2 987 0
	ldrh	r2, [r3, #86]
.LVL688:
	mov	r1, #255
	bl	ftl_memset
.LVL689:
	b	.L816
.L821:
	.align	2
.L820:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
.LFE217:
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlScanSysBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
.LFB220:
	.loc 2 1144 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI265:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI266:
	sub	fp, ip, #4
.LCFI267:
	sub	sp, sp, #24
	.loc 2 1150 0
	mov	r5, #0
	ldr	r4, .L903
	.loc 2 1152 0
	mov	r1, r5
	ldr	r2, [r4, #104]
	.loc 2 1150 0
	add	r3, r4, #3904
	.loc 2 1152 0
	ldr	r0, [r4, #3852]
	.loc 2 1158 0
	mov	r6, r4
	.loc 2 1150 0
	strh	r5, [r3]	@ movhi
	.loc 2 1151 0
	strh	r5, [r4, #120]	@ movhi
	.loc 2 1152 0
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL690:
	.loc 2 1153 0
	ldr	r2, [r4, #104]
	mov	r1, r5
	ldr	r0, [r4, #3828]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL691:
	.loc 2 1154 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #3840]
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL692:
	.loc 2 1155 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #124]
	mov	r5, r4
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL693:
.LBB418:
.LBB419:
.LBB420:
	.loc 1 202 0
	ldr	r2, .L903+4
	mvn	r3, #0
	str	r3, [r4, #2552]
	str	r3, [r2, #4]
	str	r3, [r2, #8]
.LBE420:
.LBE419:
.LBE418:
	.loc 2 1158 0
	ldrh	r3, [r4, #16]
.LVL694:
	str	r3, [fp, #-44]
.LVL695:
.L823:
	.loc 2 1158 0 is_stmt 0 discriminator 1
	ldrh	r3, [r6, #18]
.LVL696:
	ldr	r2, [fp, #-44]
.LVL697:
	cmp	r3, r2
	bls	.L864
	.loc 2 1169 0 is_stmt 1 discriminator 1
	ldr	r1, [r6, #3152]
	mov	r7, #0
.LVL698:
	.loc 2 1162 0 discriminator 1
	ldrh	r2, [r6, #8]
.LVL699:
	.loc 2 1169 0 discriminator 1
	mov	r10, r7
	.loc 2 1167 0 discriminator 1
	ldr	r8, [r6, #3792]
	.loc 2 1168 0 discriminator 1
	ldr	r3, [r6, #3148]
	.loc 2 1169 0 discriminator 1
	str	r1, [fp, #-48]
	ldrh	r9, [r6, #88]
	.loc 2 1164 0 discriminator 1
	ldr	ip, .L903+8
	b	.L865
.L825:
	.loc 2 1164 0 is_stmt 0
	ldrb	r0, [ip, r7]	@ zero_extendqisi2
	ldr	r1, [fp, #-44]
	str	r3, [fp, #-64]
	str	r2, [fp, #-60]
	str	ip, [fp, #-56]
	bl	V2P_block
.LVL700:
	str	r0, [fp, #-52]
	.loc 2 1165 0 is_stmt 1
	bl	FtlBbmIsBadBlock
.LVL701:
	cmp	r0, #0
	ldr	ip, [fp, #-56]
	ldr	r2, [fp, #-60]
	ldr	r3, [fp, #-64]
	bne	.L824
	.loc 2 1167 0
	mov	r1, #20
	mla	r0, r1, r10, r8
	ldr	r1, [fp, #-52]
	lsl	r1, r1, #10
	.loc 2 1168 0
	stmib	r0, {r1, r3}
	.loc 2 1169 0
	mul	r1, r9, r10
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	ldr	lr, [fp, #-48]
	bic	r1, r1, #3
	add	r1, lr, r1
	str	r1, [r0, #12]
	.loc 2 1170 0
	add	r1, r10, #1
	uxth	r10, r1
.L824:
	add	r7, r7, #1
.L865:
	.loc 2 1162 0 discriminator 1
	uxth	r1, r7
	cmp	r2, r1
	bhi	.L825
	.loc 2 1173 0
	cmp	r10, #0
	bne	.L826
.LVL702:
.L863:
	.loc 2 1158 0
	ldr	r3, [fp, #-44]
.LVL703:
	add	r3, r3, #1
.LVL704:
	uxth	r3, r3
.LVL705:
	str	r3, [fp, #-44]
	b	.L823
.LVL706:
.L826:
	.loc 2 1176 0
	mov	r2, #1
	mov	r1, r10
	mov	r0, r8
	bl	FlashReadPages
.LVL707:
	.loc 2 1177 0
	mov	r3, #0
.LVL708:
.L902:
	str	r3, [fp, #-48]
	.loc 2 1177 0 is_stmt 0 discriminator 2
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L863
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	.loc 2 1178 0 is_stmt 1
	ldr	r3, [r4, #3792]
	add	r2, r3, r9
	.loc 2 1181 0
	ldr	r3, [r3, r9]
	.loc 2 1178 0
	ldr	r1, [r2, #4]
	.loc 2 1179 0
	ldr	r7, [r2, #12]
	.loc 2 1181 0
	cmn	r3, #1
	.loc 2 1178 0
	lsr	ip, r1, #10
	uxth	r8, ip
	.loc 2 1181 0
	bne	.L830
	mov	r3, #16
.L832:
	.loc 2 1183 0
	ldr	r0, [r4, #3792]
	str	ip, [fp, #-56]
	str	r3, [fp, #-52]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	add	r2, r2, #1
	str	r2, [r0, #4]
	.loc 2 1184 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL709:
	.loc 2 1185 0
	ldrh	r2, [r7]
	movw	r3, #65535
	ldr	ip, [fp, #-56]
	cmp	r2, r3
	ldr	r3, [fp, #-52]
	bne	.L829
	.loc 2 1186 0
	ldr	r3, [r4, #3792]
	mvn	r2, #0
	str	r2, [r3, r9]
	.loc 2 1194 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L830
.L831:
	.loc 2 1336 0
	mov	r1, #0
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
.LVL710:
	b	.L835
.L829:
	.loc 2 1188 0
	ldr	r2, [r4, #3792]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L830
	sub	r3, r3, #1
	uxth	r3, r3
	.loc 2 1182 0 discriminator 2
	cmp	r3, #0
	bne	.L832
	b	.L831
.L830:
	.loc 2 1196 0
	ldr	r2, [r6, #2516]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L833
	.loc 2 1196 0 is_stmt 0 discriminator 1
	cmp	r2, r3
	bhi	.L834
.L833:
	.loc 2 1198 0 is_stmt 1
	cmn	r3, #1
	.loc 2 1201 0
	addne	r2, r3, #1
	strne	r2, [r5, #2516]
.L834:
	.loc 2 1206 0
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L836
	bhi	.L837
	movw	r3, #61574
	cmp	r2, r3
	beq	.L838
.L835:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L902
.L837:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L839
	movw	r3, #65535
	cmp	r2, r3
	beq	.L831
	b	.L835
.L839:
	.loc 2 1211 0
	ldr	r9, .L903+12
	ldr	r3, [r4, #104]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L841
	.loc 2 1211 0 is_stmt 0 discriminator 1
	movw	r2, #1211
	ldr	r1, .L903+16
	ldr	r0, .L903+20
	bl	printk
.LVL711:
.L841:
	.loc 2 1212 0 is_stmt 1
	ldr	r0, [r4, #104]
	ldrh	r2, [r9]
	.loc 2 1214 0
	ldr	ip, [r4, #3852]
	.loc 2 1212 0
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
	str	r1, [fp, #-52]
.L842:
	.loc 2 1212 0 is_stmt 0 discriminator 1
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L848
	.loc 2 1235 0 is_stmt 1
	cmp	r3, #0
	bge	.L880
	b	.L835
.L848:
	.loc 2 1214 0
	lsl	lr, r3, #2
	ldr	r1, [r7, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L843
	.loc 2 1216 0
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L844
	.loc 2 1216 0 is_stmt 0 discriminator 1
	cmp	r0, r2
	.loc 2 1221 0 is_stmt 1 discriminator 1
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L844:
	.loc 2 1224 0 discriminator 1
	uxth	ip, r3
	mov	r1, #0
.L845:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L846
	.loc 2 1229 0
	ldr	r1, [r7, #4]
	.loc 2 1235 0
	cmp	r3, #0
	.loc 2 1229 0
	ldr	r2, [r5, #3852]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	.loc 2 1230 0
	lsl	r2, r3, #1
	ldr	r1, [r5, #3828]
	strh	r8, [r1, r2]	@ movhi
	.loc 2 1235 0
	blt	.L835
	.loc 2 1242 0
	ldr	r2, .L903+12
	ldr	r1, [r5, #104]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L835
.L880:
	.loc 2 1244 0
	add	r2, r2, #1
	.loc 2 1245 0
	ldr	r1, [r7, #4]
	.loc 2 1244 0
	strh	r2, [r9]	@ movhi
	.loc 2 1245 0
	ldr	r2, [r4, #3852]
	str	r1, [r2, r3, lsl #2]
	.loc 2 1246 0
	lsl	r3, r3, #1
	ldr	r2, [r4, #3828]
.L900:
	.loc 2 1290 0
	strh	r8, [r2, r3]	@ movhi
	b	.L835
.L846:
	.loc 2 1226 0 discriminator 3
	ldr	r0, [r6, #3852]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	.loc 2 1227 0 discriminator 3
	lsl	r2, r2, #1
	ldr	r0, [r6, #3828]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L845
.L843:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L842
.L838:
	.loc 2 1252 0
	ldrh	r2, [r4, #120]
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bls	.L851
	.loc 2 1252 0 is_stmt 0 discriminator 1
	movw	r2, #1252
	ldr	r1, .L903+16
	ldr	r0, .L903+20
	bl	printk
.LVL712:
.L851:
	.loc 2 1253 0 is_stmt 1
	ldrh	r2, [r4, #96]
	ldrh	r1, [r4, #120]
	.loc 2 1255 0
	ldr	ip, [r4, #3840]
	.loc 2 1253 0
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L852:
	.loc 2 1253 0 is_stmt 0 discriminator 1
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L857
	.loc 2 1255 0 is_stmt 1
	ldr	r9, [ip, r3, lsl #2]
	lsl	lr, r3, #2
	ldr	r0, [r7, #4]
	cmp	r0, r9
	bls	.L853
	.loc 2 1257 0
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	.loc 2 1267 0
	uxth	ip, r3
	.loc 2 1257 0
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	cmp	r2, #0
	.loc 2 1265 0
	addeq	r1, r1, #1
	strheq	r1, [r5, #120]	@ movhi
	mov	r1, #0
.L855:
	.loc 2 1267 0 discriminator 1
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L856
	.loc 2 1272 0
	ldr	r1, [r7, #4]
	ldr	r2, [r5, #3840]
	str	r1, [r2, lr]
	.loc 2 1273 0
	lsl	r2, r3, #1
	ldr	r1, [r5, #124]
	strh	r8, [r1, r2]	@ movhi
.L857:
	.loc 2 1278 0
	cmp	r3, #0
	blt	.L835
	.loc 2 1286 0
	ldrh	r2, [r5, #96]
	ldrh	r1, [r5, #120]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L835
	.loc 2 1288 0
	add	r1, r1, #1
	.loc 2 1289 0
	ldr	r2, [r5, #3840]
	.loc 2 1288 0
	strh	r1, [r5, #120]	@ movhi
	.loc 2 1289 0
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	.loc 2 1290 0
	lsl	r3, r3, #1
	ldr	r2, [r5, #124]
	b	.L900
.L856:
	.loc 2 1269 0 discriminator 3
	ldr	r0, [r6, #3840]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	.loc 2 1270 0 discriminator 3
	lsl	r2, r2, #1
	ldr	r0, [r6, #124]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L855
.L853:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L852
.L836:
	.loc 2 1297 0
	ldr	r9, .L903+4
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	.loc 2 1299 0
	strheq	r8, [r9]	@ movhi
	.loc 2 1297 0
	beq	.L901
	.loc 2 1304 0
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L860
	.loc 2 1305 0
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.LVL713:
.L860:
	.loc 2 1306 0
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2560]
	cmp	r2, r3
	.loc 2 1314 0
	strhcs	r8, [r9, #4]	@ movhi
	.loc 2 1306 0
	bcs	.L835
	.loc 2 1308 0
	ldrh	r3, [r9]
	.loc 2 1309 0
	strh	r8, [r9]	@ movhi
	.loc 2 1308 0
	strh	r3, [r9, #4]	@ movhi
	.loc 2 1310 0
	ldr	r3, [r7, #4]
.L901:
	str	r3, [r4, #2560]
	b	.L835
.LVL714:
.L864:
	.loc 2 1341 0
	ldr	r2, [r6, #3828]
.LVL715:
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L866
.L869:
	.loc 2 1359 0
	ldr	r1, [r4, #124]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L867
.L868:
	.loc 2 1377 0
	ldr	r3, .L903+12
	ldrh	r2, [r3]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L899
	.loc 2 1377 0 is_stmt 0 discriminator 1
	movw	r2, #1377
	ldr	r1, .L903+16
	ldr	r0, .L903+20
	bl	printk
.LVL716:
.L899:
	.loc 2 1379 0 is_stmt 1
	mov	r0, #0
	sub	sp, fp, #40
.LCFI268:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL717:
.L866:
.LCFI269:
	.loc 2 1341 0 discriminator 1
	ldr	r1, .L903+12
	ldrh	r1, [r1]
	cmp	r1, #0
	.loc 2 1343 0 discriminator 1
	ldrne	r1, [r6, #104]
	.loc 2 1341 0 discriminator 1
	beq	.L869
.L870:
	sxth	ip, r3
	.loc 2 1343 0 discriminator 1
	cmp	ip, r1
	bcs	.L869
	.loc 2 1345 0
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L870
	mov	r3, ip
	.loc 2 1351 0 discriminator 1
	mov	lr, #0
.L871:
	.loc 2 1347 0 discriminator 1
	ldr	r2, [r5, #104]
	cmp	r3, r2
	bcs	.L869
	.loc 2 1349 0 discriminator 3
	ldr	r1, [r5, #3828]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
.LVL718:
	strh	r7, [r1, r6]	@ movhi
	.loc 2 1350 0 discriminator 3
	ldr	r1, [r5, #3852]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	.loc 2 1351 0 discriminator 3
	ldr	r1, [r5, #3828]
	strh	lr, [r1, r2]	@ movhi
	b	.L871
.LVL719:
.L867:
	.loc 2 1359 0 discriminator 1
	ldrh	r3, [r4, #120]
	cmp	r3, #0
	.loc 2 1361 0 discriminator 1
	ldrhne	r0, [r4, #96]
	.loc 2 1359 0 discriminator 1
	beq	.L868
.L876:
	sxth	r3, r2
	.loc 2 1361 0 discriminator 1
	cmp	r3, r0
	mov	ip, r3
	bge	.L868
	.loc 2 1363 0
	lsl	lr, r3, #1
	add	r2, r2, #1
	ldrh	lr, [r1, lr]
	cmp	lr, #0
	beq	.L876
	.loc 2 1369 0 discriminator 1
	mov	lr, #0
.L877:
	.loc 2 1365 0 discriminator 1
	ldrh	r2, [r5, #96]
	cmp	r3, r2
	bge	.L868
	.loc 2 1367 0 discriminator 3
	ldr	r1, [r5, #124]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
.LVL720:
	strh	r7, [r1, r6]	@ movhi
	.loc 2 1368 0 discriminator 3
	ldr	r1, [r5, #3840]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	.loc 2 1369 0 discriminator 3
	ldr	r1, [r5, #124]
	strh	lr, [r1, r2]	@ movhi
	b	.L877
.L904:
	.align	2
.L903:
	.word	.LANCHOR0
	.word	.LANCHOR0+2552
	.word	.LANCHOR0+36
	.word	.LANCHOR0+3904
	.word	.LANCHOR1+328
	.word	.LC1
.LFE220:
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
.LFB225:
	.loc 2 1515 0
	@ args = 0, pretend = 0, frame = 20
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL721:
	mov	ip, sp
.LCFI270:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI271:
	sub	fp, ip, #4
.LCFI272:
	sub	sp, sp, #20
	.loc 2 1526 0
	mov	r1, #0
	.loc 2 1519 0
	ldr	r3, [r0, #24]
.LVL722:
	.loc 2 1515 0
	mov	r4, r0
	.loc 2 1527 0
	ldr	r6, .L932
	.loc 2 1532 0
	mov	r7, #0
	.loc 2 1520 0
	ldr	r8, [r0, #12]
.LVL723:
	.loc 2 1519 0
	str	r3, [fp, #-44]
	.loc 2 1521 0
	ldr	r3, [r0, #16]
.LVL724:
	mov	r5, r6
	str	r3, [fp, #-56]
	.loc 2 1522 0
	ldrh	r3, [r0, #6]
.LVL725:
	str	r3, [fp, #-48]
	.loc 2 1523 0
	ldrh	r3, [r0, #8]
.LVL726:
	.loc 2 1526 0
	ldr	r0, [fp, #-44]
.LVL727:
	.loc 2 1523 0
	str	r3, [fp, #-52]
	.loc 2 1526 0
	ldr	r3, [fp, #-48]
.LVL728:
	lsl	r2, r3, #2
	bl	ftl_memset
.LVL729:
	.loc 2 1527 0
	ldr	r3, [r6, #3240]
	.loc 2 1530 0
	mvn	r2, #0
	.loc 2 1528 0
	ldr	r9, [r6, #3244]
	.loc 2 1532 0
	str	r7, [r4, #32]
	.loc 2 1527 0
	str	r3, [r6, #3228]
	.loc 2 1528 0
	str	r9, [r6, #3232]
	.loc 2 1530 0
	strh	r2, [r4]	@ movhi
	.loc 2 1531 0
	strh	r2, [r4, #2]	@ movhi
	.loc 2 1534 0
	mov	r2, #1
	.loc 2 1533 0
	str	r7, [r4, #28]
	.loc 2 1534 0
	str	r2, [r4, #36]
.LVL730:
.L906:
	.loc 2 1535 0 discriminator 1
	ldr	r3, [fp, #-52]
.LVL731:
	sxth	r2, r7
.LVL732:
	cmp	r2, r3
	bge	.L923
	.loc 2 1537 0
	ldr	r3, [fp, #-52]
	lsl	r0, r2, #1
	sub	r1, r3, #1
	cmp	r2, r1
	bne	.L907
	.loc 2 1539 0
	add	r10, r8, r0
	mov	r1, #1
	ldrh	r0, [r8, r0]
	.loc 2 1543 0
	mov	r8, #0
.LVL733:
	str	r2, [fp, #-52]
	.loc 2 1539 0
	bl	FtlGetLastWrittenPage
.LVL734:
	.loc 2 1542 0
	ldr	r3, [fp, #-56]
.LVL735:
	.loc 2 1539 0
	sxth	r1, r0
.LVL736:
	.loc 2 1542 0
	ldr	r2, [fp, #-52]
.LVL737:
	.loc 2 1540 0
	add	r0, r0, #1
	.loc 2 1546 0
	ldr	ip, .L932+4
	.loc 2 1541 0
	strh	r7, [r4]	@ movhi
	.loc 2 1543 0
	add	r7, r1, #1
	.loc 2 1540 0
	strh	r0, [r4, #2]	@ movhi
	.loc 2 1542 0
	ldr	r2, [r3, r2, lsl #2]
.LVL738:
	str	r2, [r4, #28]
.LVL739:
.L908:
	.loc 2 1543 0 discriminator 1
	sxth	r2, r8
	cmp	r2, r7
	blt	.L910
.LVL740:
.L923:
	.loc 2 1596 0
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
.LVL741:
	.loc 2 1597 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bne	.L912
	.loc 2 1599 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL742:
.L912:
	.loc 2 1602 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL743:
	.loc 2 1604 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL744:
	.loc 2 1606 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI273:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL745:
.L910:
.LCFI274:
	.loc 2 1545 0
	ldrh	r1, [r10]
.LVL746:
	.loc 2 1546 0
	mov	r0, ip
	str	ip, [fp, #-52]
	.loc 2 1545 0
	orr	r2, r2, r1, lsl #10
	str	r2, [r6, #3224]
	.loc 2 1546 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL747:
	.loc 2 1549 0
	ldr	r2, [r6, #3220]
	ldr	ip, [fp, #-52]
	cmn	r2, #1
	beq	.L909
	.loc 2 1551 0
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
.LVL748:
	cmp	r3, r2
	bls	.L909
	.loc 2 1551 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	.loc 2 1553 0 is_stmt 1 discriminator 1
	ldreq	r1, [r6, #3224]
	ldreq	r3, [fp, #-44]
.LVL749:
	streq	r1, [r3, r2, lsl #2]
.LVL750:
.L909:
	add	r8, r8, #1
	b	.L908
.LVL751:
.L907:
	.loc 2 1559 0
	ldr	r2, [r5, #3240]
.LVL752:
	.loc 2 1560 0
	add	r3, r8, r0
.LVL753:
	str	r3, [fp, #-60]
	.loc 2 1559 0
	str	r2, [r5, #3228]
	.loc 2 1560 0
	ldrh	r2, [r5, #80]
	ldrh	r1, [r8, r0]
	.loc 2 1561 0
	ldr	r0, .L932+4
	.loc 2 1560 0
	sub	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3224]
	.loc 2 1561 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL754:
	.loc 2 1564 0
	ldr	r2, [r5, #3220]
	cmn	r2, #1
	beq	.L925
	.loc 2 1564 0 is_stmt 0 discriminator 1
	ldrh	r1, [r9]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L925
	.loc 2 1564 0 discriminator 2
	ldrh	r1, [r9, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L914
.L925:
	mov	r10, #0
.LVL755:
.L915:
	.loc 2 1579 0 is_stmt 1 discriminator 1
	ldrh	r1, [r5, #80]
	sxth	r2, r10
	cmp	r2, r1
	bge	.L921
	.loc 2 1581 0
	ldr	r3, [fp, #-60]
.LVL756:
	.loc 2 1582 0
	ldr	r0, .L932+4
	.loc 2 1581 0
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3224]
	.loc 2 1582 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL757:
	.loc 2 1585 0
	ldr	r2, [r5, #3220]
	cmn	r2, #1
	beq	.L919
	.loc 2 1587 0
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
.LVL758:
	cmp	r3, r2
	bls	.L919
	.loc 2 1587 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	.loc 2 1589 0 is_stmt 1 discriminator 1
	ldreq	r1, [r5, #3224]
	ldreq	r3, [fp, #-44]
.LVL759:
	streq	r1, [r3, r2, lsl #2]
.LVL760:
.L919:
	add	r10, r10, #1
	b	.L915
.LVL761:
.L914:
	mov	r0, #0
	.loc 2 1572 0 discriminator 1
	mov	lr, #4
.LVL762:
.L916:
	.loc 2 1567 0 discriminator 1
	ldrh	r1, [r5, #80]
	sxth	r2, r0
	sub	r1, r1, #1
	cmp	r2, r1
	blt	.L918
.LVL763:
.L921:
	add	r7, r7, #1
	b	.L906
.LVL764:
.L918:
	.loc 2 1569 0
	ldr	ip, [r5, #3240]
	add	r0, r0, #1
	.loc 2 1570 0
	ldr	r3, [fp, #-48]
.LVL765:
	.loc 2 1569 0
	ldr	r1, [ip, r2, lsl #3]
	.loc 2 1570 0
	uxth	r10, r1
	cmp	r3, r10
	.loc 2 1572 0
	addhi	r2, lr, r2, lsl #3
	ldrhi	r3, [fp, #-44]
.LVL766:
	movhi	r1, r10
	ldrhi	r2, [ip, r2]
	strhi	r2, [r3, r1, lsl #2]
	b	.L916
.L933:
	.align	2
.L932:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
.LFE225:
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
.LFB226:
	.loc 2 1609 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI275:
	push	{fp, ip, lr, pc}
.LCFI276:
	sub	fp, ip, #4
.LCFI277:
	.loc 2 1610 0
	ldr	r3, .L935
	ldrh	r2, [r3, #96]
	add	r0, r3, #3904
	add	r0, r0, #4
	strh	r2, [r0, #10]	@ movhi
	.loc 2 1611 0
	ldr	r2, .L935+4
	strh	r2, [r0, #4]	@ movhi
	.loc 2 1612 0
	ldrh	r2, [r3, #120]
	strh	r2, [r0, #8]	@ movhi
	.loc 2 1613 0
	ldrh	r2, [r3, #98]
	strh	r2, [r0, #6]	@ movhi
	.loc 2 1614 0
	ldr	r2, [r3, #124]
	str	r2, [r3, #3920]
	.loc 2 1615 0
	ldr	r2, [r3, #3840]
	str	r2, [r3, #3924]
	.loc 2 1616 0
	ldr	r2, [r3, #3836]
	str	r2, [r3, #3928]
	.loc 2 1617 0
	ldr	r2, [r3, #3844]
	str	r2, [r3, #3932]
	.loc 2 1619 0
	bl	FtlMapTblRecovery
.LVL767:
	.loc 2 1622 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L936:
	.align	2
.L935:
	.word	.LANCHOR0
	.word	-3962
.LFE226:
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlL2PDataInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
.LFB227:
	.loc 2 1625 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI278:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI279:
	sub	fp, ip, #4
.LCFI280:
	.loc 2 1627 0
	ldr	r4, .L940
	mov	r1, #0
	.loc 2 1632 0
	mvn	r5, #0
	.loc 2 1627 0
	ldr	r2, [r4, #104]
	ldr	r0, [r4, #3832]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL768:
	.loc 2 1628 0
	ldrh	r3, [r4, #86]
	mov	r1, #255
	ldrh	r2, [r4, #114]
	ldr	r0, [r4, #3856]
	mul	r2, r2, r3
	bl	ftl_memset
.LVL769:
	.loc 2 1629 0
	mov	r2, #0
	mov	r3, r4
	.loc 2 1631 0
	mov	lr, #12
	mov	r4, r2
.L938:
	.loc 2 1629 0 discriminator 1
	ldrh	r0, [r3, #114]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r0, r1
	bhi	.L939
	.loc 2 1635 0
	ldr	r2, .L940+4
	mvn	r1, #0
	.loc 2 1636 0
	movw	r0, #3864
	strh	r1, [r3, r0]	@ movhi
	.loc 2 1635 0
	strh	r1, [r2, #2]	@ movhi
	.loc 2 1637 0
	ldr	r1, [r3, #104]
	strh	r1, [r2, #10]	@ movhi
	.loc 2 1638 0
	ldr	r1, .L940+8
	strh	r1, [r2, #4]	@ movhi
	.loc 2 1639 0
	ldrh	r1, [r2, #40]
	strh	r1, [r2, #8]	@ movhi
	.loc 2 1640 0
	ldrh	r1, [r3, #112]
	strh	r1, [r2, #6]	@ movhi
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
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L939:
	.loc 2 1631 0 discriminator 3
	uxth	r2, r2
	ldr	r1, [r3, #2468]
	mul	r0, lr, r2
	add	r6, r1, r0
	str	r4, [r6, #4]
	.loc 2 1632 0 discriminator 3
	strh	r5, [r1, r0]	@ movhi
	.loc 2 1633 0 discriminator 3
	ldr	r1, [r3, #2468]
	add	r1, r1, r0
	ldrh	r0, [r3, #86]
	mul	r2, r2, r0
	ldr	r0, [r3, #3856]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L938
.L941:
	.align	2
.L940:
	.word	.LANCHOR0
	.word	.LANCHOR0+3864
	.word	-3902
.LFE227:
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlLoadMapInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
.LFB228:
	.loc 2 1648 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI281:
	push	{fp, ip, lr, pc}
.LCFI282:
	sub	fp, ip, #4
.LCFI283:
	.loc 2 1649 0
	bl	FtlL2PDataInit
.LVL770:
	.loc 2 1651 0
	ldr	r0, .L943
	bl	FtlMapTblRecovery
.LVL771:
	.loc 2 1654 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L944:
	.align	2
.L943:
	.word	.LANCHOR0+3864
.LFE228:
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVariablesInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
.LFB157:
	.loc 3 493 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI284:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI285:
	sub	fp, ip, #4
.LCFI286:
	.loc 3 496 0
	ldr	r4, .L946
	.loc 3 497 0
	mvn	r3, #0
	.loc 3 496 0
	mov	r5, #0
	.loc 3 502 0
	mov	r1, r5
	.loc 3 497 0
	add	r2, r4, #3952
	.loc 3 502 0
	ldr	r0, [r4, #124]
	.loc 3 497 0
	strh	r3, [r2]	@ movhi
	.loc 3 502 0
	ldrh	r2, [r4, #96]
	.loc 3 500 0
	str	r3, [r4, #3964]
	.loc 3 496 0
	str	r5, [r4, #3948]
	.loc 3 498 0
	str	r5, [r4, #3956]
	.loc 3 502 0
	lsl	r2, r2, #1
	.loc 3 499 0
	str	r5, [r4, #3960]
	.loc 3 501 0
	strh	r5, [r4, #120]	@ movhi
	.loc 3 502 0
	bl	ftl_memset
.LVL772:
	.loc 3 503 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #2304]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL773:
	.loc 3 504 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #3816]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL774:
.LBB431:
.LBB432:
.LBB433:
	.loc 1 202 0
	add	r0, r4, #2240
	mov	r1, #48
	add	r0, r0, #4
	bl	__memzero
.LVL775:
.LBE433:
.LBE432:
.LBE431:
.LBB434:
.LBB435:
.LBB436:
	add	r0, r4, #2608
	mov	r1, #512
	add	r0, r0, #4
	bl	__memzero
.LVL776:
.LBE436:
.LBE435:
.LBE434:
	.loc 3 507 0
	bl	FtlGcBufInit
.LVL777:
	.loc 3 508 0
	bl	FtlL2PDataInit
.LVL778:
	.loc 3 510 0
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L947:
	.align	2
.L946:
	.word	.LANCHOR0
.LFE157:
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
.LFB235:
	.loc 2 2116 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI287:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI288:
	sub	fp, ip, #4
.LCFI289:
	sub	sp, sp, #16
	.loc 2 2121 0
	mov	r5, #0
	.loc 2 2120 0
	ldr	r4, .L959
	mov	r1, #0
	.loc 2 2126 0
	mov	r8, r5
.LVL779:
	mov	r7, r5
.LVL780:
	.loc 2 2120 0
	ldrh	r2, [r4, #18]
	ldr	r0, [r4, #2292]
	lsl	r2, r2, #3
	bl	ftl_memset
.LVL781:
	.loc 2 2124 0
	movw	r3, #2312
	.loc 2 2121 0
	str	r5, [r4, #2316]
	.loc 2 2124 0
	strh	r5, [r4, r3]	@ movhi
	.loc 2 2125 0
	add	r3, r4, #2320
	.loc 2 2122 0
	str	r5, [r4, #2296]
	.loc 2 2123 0
	str	r5, [r4, #2308]
	.loc 2 2125 0
	strh	r5, [r3]	@ movhi
	str	r3, [fp, #-48]
.LVL782:
.L949:
	.loc 2 2128 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L955
	.loc 2 2133 0
	ldrh	r3, [r4, #78]
	mov	r9, r10
	.loc 2 2130 0
	ldrh	ip, [r4, #8]
	.loc 2 2133 0
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r6, r3
	b	.L956
.L951:
	.loc 2 2131 0
	ldr	r2, .L959+4
	mov	r1, r9
	str	ip, [fp, #-56]
	str	r3, [fp, #-52]
	ldrb	r0, [r2, r3]	@ zero_extendqisi2
	bl	V2P_block
.LVL783:
	.loc 2 2132 0
	bl	FtlBbmIsBadBlock
.LVL784:
	cmp	r0, #0
	ldr	r3, [fp, #-52]
	.loc 2 2133 0
	ldreq	r2, [fp, #-44]
	.loc 2 2132 0
	ldr	ip, [fp, #-56]
	add	r3, r3, #1
	.loc 2 2133 0
	addeq	r6, r6, r2
	uxtheq	r6, r6
.L956:
	.loc 2 2130 0 discriminator 1
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L951
	.loc 2 2135 0
	cmp	r6, #0
	moveq	r0, r6
	beq	.L952
	.loc 2 2136 0
	mov	r1, r6
	mov	r0, #32768
	bl	__divsi3
.LVL785:
.L952:
	.loc 2 2137 0
	ldr	r3, [r4, #2292]
	add	r3, r3, r9, lsl #3
	strh	r0, [r3, #4]	@ movhi
	.loc 2 2139 0
	ldr	r3, .L959+8
	ldrh	r1, [r3]
	cmp	r1, r10
	beq	.L953
	.loc 2 2139 0 is_stmt 0 discriminator 1
	ldrh	r1, [r3, #48]
	cmp	r1, r10
	beq	.L953
	.loc 2 2140 0 is_stmt 1
	ldrh	r3, [r3, #96]
	cmp	r3, r10
	beq	.L953
	.loc 2 2143 0
	ldr	r2, [r4, #2300]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L954
	.loc 2 2144 0
	add	r8, r8, #1
.LVL786:
	.loc 2 2145 0
	mov	r0, r9
	.loc 2 2144 0
	uxth	r8, r8
.LVL787:
	.loc 2 2145 0
	bl	INSERT_FREE_LIST
.LVL788:
.L953:
	add	r5, r5, #1
	b	.L949
.L954:
	.loc 2 2147 0
	add	r7, r7, #1
.LVL789:
	.loc 2 2148 0
	mov	r0, r9
	.loc 2 2147 0
	uxth	r7, r7
.LVL790:
	.loc 2 2148 0
	bl	INSERT_DATA_LIST
.LVL791:
	b	.L953
.L955:
	.loc 2 2152 0
	movw	r2, #2312
	strh	r7, [r4, r2]	@ movhi
	.loc 2 2154 0
	add	r7, r7, r8
.LVL792:
	.loc 2 2153 0
	ldr	r2, [fp, #-48]
	.loc 2 2154 0
	cmp	r7, r3
	.loc 2 2153 0
	strh	r8, [r2]	@ movhi
	.loc 2 2154 0
	ble	.L957
	.loc 2 2154 0 is_stmt 0 discriminator 1
	movw	r2, #2154
	ldr	r1, .L959+12
	ldr	r0, .L959+16
	bl	printk
.LVL793:
.L957:
	.loc 2 2156 0 is_stmt 1
	mov	r0, #0
	sub	sp, fp, #40
.LCFI290:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL794:
.L960:
	.align	2
.L959:
	.word	.LANCHOR0
	.word	.LANCHOR0+36
	.word	.LANCHOR0+2324
	.word	.LANCHOR1+342
	.word	.LC1
.LFE235:
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
.LFB259:
	.loc 5 202 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI291:
	push	{r4, fp, ip, lr, pc}
.LCFI292:
	sub	fp, ip, #4
.LCFI293:
	.loc 5 203 0
	ldr	r4, .L962
	mov	r3, #0
	.loc 5 205 0
	mov	r1, #255
	.loc 5 203 0
	add	r2, r4, #3168
	.loc 5 205 0
	ldr	r0, [r4, #3164]
	.loc 5 203 0
	strh	r3, [r2]	@ movhi
	.loc 5 204 0
	movw	r2, #3176
	strh	r3, [r4, r2]	@ movhi
	.loc 5 205 0
	ldrh	r2, [r4, #82]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL795:
	.loc 5 206 0
	ldrh	r3, [r4, #82]
	mov	r2, #12
	mov	r1, #255
	ldr	r0, [r4, #3172]
	mul	r2, r2, r3
	bl	ftl_memset
.LVL796:
	.loc 5 207 0
	bl	FtlGcBufInit
.LVL797:
	ldmfd	sp, {r4, fp, sp, pc}
.L963:
	.align	2
.L962:
	.word	.LANCHOR0
.LFE259:
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
.LFB260:
	.loc 5 211 0
	@ args = 0, pretend = 0, frame = 20
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL798:
	mov	ip, sp
.LCFI294:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI295:
	sub	fp, ip, #4
.LCFI296:
	sub	sp, sp, #20
	.loc 5 211 0
	mov	r6, r0
	.loc 5 220 0
	ldr	r3, .L987
	.loc 5 211 0
	mov	r10, r1
	.loc 5 220 0
	ldrh	r4, [r3, #4]
.LVL799:
	.loc 5 221 0
	movw	r3, #65535
	cmp	r4, r3
	beq	.L981
	.loc 5 223 0
	cmp	r4, #0
	bne	.L965
.L966:
	.loc 5 224 0
	bl	FtlGcPageVarInit
.LVL800:
	b	.L967
.LVL801:
.L981:
	.loc 5 222 0
	mov	r4, #0
.L965:
	.loc 5 223 0 discriminator 1
	ldr	r3, .L987+4
	ldrh	r3, [r3, #78]
	cmp	r3, r10
	beq	.L966
.L967:
	.loc 5 231 0
	ldr	r5, .L987+4
	.loc 5 222 0
	mov	r8, #0
.LVL802:
.L975:
	.loc 5 228 0
	ldrh	r2, [r6]
	movw	r3, #65535
	.loc 5 226 0
	mov	r0, #0
	strb	r0, [r6, #8]
	.loc 5 228 0
	cmp	r2, r3
	beq	.L968
.L978:
	.loc 5 231 0
	ldrh	r3, [r5, #8]
	mov	r2, #0
	.loc 5 234 0
	ldr	r0, [r5, #3792]
	add	ip, r6, #16
	.loc 5 230 0
	mov	r7, r2
	.loc 5 233 0
	movw	r9, #65535
	.loc 5 231 0
	str	r3, [fp, #-44]
	.loc 5 235 0
	ldr	r3, [r5, #3148]
	str	r3, [fp, #-48]
	ldrh	r3, [r5, #86]
	str	r3, [fp, #-52]
	.loc 5 236 0
	ldr	r3, [r5, #3152]
	str	r3, [fp, #-56]
	ldrh	r3, [r5, #88]
	str	r3, [fp, #-60]
.LVL803:
.L969:
	.loc 5 231 0 discriminator 1
	ldr	r1, [fp, #-44]
	uxth	r3, r2
.LVL804:
	cmp	r1, r3
	bhi	.L971
	.loc 5 241 0
	mov	r9, #0
	.loc 5 240 0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.LVL805:
.L972:
	.loc 5 241 0 discriminator 1
	uxth	r3, r9
	cmp	r7, r3
	bhi	.L976
	.loc 5 266 0
	add	r8, r8, #1
	.loc 5 264 0
	add	r4, r4, #1
.LVL806:
	.loc 5 268 0
	cmp	r10, r8
	.loc 5 264 0
	uxth	r4, r4
.LVL807:
	.loc 5 268 0
	bls	.L977
.L979:
	.loc 5 274 0
	ldrh	r3, [r5, #78]
	cmp	r3, r4
	bhi	.L978
	.loc 5 265 0
	mov	r0, #0
.LVL808:
	b	.L968
.LVL809:
.L971:
	.loc 5 232 0
	ldrh	r3, [ip], #2
.LVL810:
	.loc 5 233 0
	cmp	r3, r9
	beq	.L970
	.loc 5 234 0
	mov	r1, #20
	orr	r3, r4, r3, lsl #10
.LVL811:
	mla	r1, r1, r7, r0
	str	r3, [r1, #4]
	.loc 5 235 0
	ldr	r3, [fp, #-52]
	mul	r3, r3, r7
	add	lr, r3, #3
	cmp	r3, #0
	movlt	r3, lr
	ldr	lr, [fp, #-48]
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #8]
	.loc 5 236 0
	ldr	r3, [fp, #-60]
	mul	r3, r3, r7
	.loc 5 237 0
	add	r7, r7, #1
.LVL812:
	uxth	r7, r7
.LVL813:
	.loc 5 236 0
	add	lr, r3, #3
	cmp	r3, #0
	movlt	r3, lr
	ldr	lr, [fp, #-56]
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.LVL814:
.L970:
	add	r2, r2, #1
	b	.L969
.LVL815:
.L976:
	.loc 5 242 0
	mov	r3, #20
	ldr	r2, [r5, #3792]
	mul	r3, r3, r9
	str	r2, [fp, #-52]
	add	ip, r2, r3
	str	r3, [fp, #-56]
	ldr	r1, [ip, #4]
.LVL816:
	str	ip, [fp, #-48]
	.loc 5 243 0
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-44]
	bl	P2V_plane
.LVL817:
	.loc 5 245 0
	ldr	r2, [fp, #-52]
	ldr	r3, [fp, #-56]
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L973
	.loc 5 247 0
	ldr	ip, [fp, #-48]
	.loc 5 248 0
	movw	r2, #65535
	add	r9, r9, #1
	ldr	r1, [fp, #-44]
.LVL818:
	.loc 5 247 0
	ldr	r3, [ip, #12]
.LVL819:
	.loc 5 248 0
	ldrh	ip, [r3]
	cmp	ip, r2
	bne	.L974
	.loc 5 249 0
	mov	r3, #1
.LVL820:
	str	r3, [r5, #3960]
.LVL821:
.L968:
	.loc 5 281 0
	ldr	r3, .L987
	mvn	r2, #0
	.loc 5 283 0
	strb	r0, [r6, #6]
	.loc 5 284 0
	mov	r1, r4
.LVL822:
	.loc 5 282 0
	strh	r4, [r6, #2]	@ movhi
	.loc 5 281 0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 284 0
	mov	r2, r0
	mov	r0, r6
.LVL823:
	bl	ftl_sb_update_avl_pages
.LVL824:
	.loc 5 285 0
	b	.L980
.LVL825:
.L974:
	.loc 5 252 0
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL826:
	b	.L972
.L973:
	.loc 5 256 0
	ldrh	r3, [r6]
	mov	r4, #0
.LVL827:
	ldr	r2, [r5, #2300]
	lsl	r3, r3, #1
	strh	r4, [r2, r3]	@ movhi
	.loc 5 257 0
	ldrh	r0, [r6]
	bl	INSERT_FREE_LIST
.LVL828:
	.loc 5 258 0
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
	.loc 5 260 0
	bl	FtlGcPageVarInit
.LVL829:
	.loc 5 261 0
	b	.L975
.LVL830:
.L977:
	.loc 5 268 0 discriminator 1
	ldr	r2, .L987
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L979
	.loc 5 269 0
	add	r3, r3, r8
	strh	r3, [r2, #4]	@ movhi
	.loc 5 270 0
	ldrh	r3, [r5, #78]
	cmp	r3, r4
	bls	.L979
.L980:
	.loc 5 286 0
	mvn	r0, #0
	sub	sp, fp, #40
.LCFI297:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL831:
.L988:
	.align	2
.L987:
	.word	.LANCHOR2
	.word	.LANCHOR0
.LFE260:
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	ftl_memcpy
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcpy, %function
ftl_memcpy:
.LFB278:
	.loc 1 206 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL832:
	mov	ip, sp
.LCFI298:
	push	{fp, ip, lr, pc}
.LCFI299:
	sub	fp, ip, #4
.LCFI300:
	.loc 1 207 0
	bl	memcpy
.LVL833:
	.loc 1 208 0
	ldmfd	sp, {fp, sp, pc}
.LFE278:
	.size	ftl_memcpy, .-ftl_memcpy
	.align	2
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
.LFB176:
	.loc 4 80 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI301:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI302:
	sub	fp, ip, #4
.LCFI303:
	sub	sp, sp, #4
	.loc 4 91 0
	mov	r5, #0
.LVL834:
	.loc 4 87 0
	ldr	r4, .L999
	.loc 4 89 0
	mov	r1, #0
	.loc 4 92 0
	ldr	r7, .L999+4
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
.LVL835:
.L991:
	.loc 4 91 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	blt	.L992
	.loc 4 94 0
	ldr	r6, [r4, #3232]
.LVL836:
.LBB442:
.LBB443:
.LBB444:
	.loc 1 202 0
	mov	r2, #16
	mov	r1, #255
.LBE444:
.LBE443:
.LBE442:
	.loc 4 108 0
	ldr	r9, .L999+8
	.loc 4 109 0
	ldr	r8, .L999+12
	.loc 4 84 0
	mov	r5, #0
.LVL837:
.LBB449:
.LBB447:
.LBB445:
	.loc 1 202 0
	mov	r0, r6
.LBE445:
.LBE447:
.LBE449:
	.loc 4 82 0
	mov	r7, r5
.LVL838:
.LBB450:
.LBB448:
.LBB446:
	.loc 1 202 0
	bl	memset
.LVL839:
.LBE446:
.LBE448:
.LBE450:
	.loc 4 97 0
	ldr	r3, .L999+16
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
.L993:
	.loc 4 104 0
	ldr	r3, [r4, #3240]
	.loc 4 106 0
	mov	r10, #0
	.loc 4 107 0
	ldrh	r1, [r4, #128]
	ldrh	r2, [r4, #130]
	.loc 4 104 0
	str	r3, [r4, #3228]
	.loc 4 105 0
	ldr	r3, [r4, #3244]
	.loc 4 106 0
	str	r10, [r4, #3220]
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
.LVL840:
	.loc 4 109 0
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL841:
	.loc 4 110 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r4, #130]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L994
	.loc 4 113 0
	ldr	r3, [r4, #136]
	.loc 4 115 0
	ldrh	r2, [r4, #128]
	.loc 4 121 0
	ldr	r0, [r4, #3760]
	.loc 4 113 0
	add	r3, r3, #1
	.loc 4 117 0
	strh	r10, [r4, #130]	@ movhi
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
	mov	r2, #1
	.loc 4 116 0
	strh	r3, [r4, #128]	@ movhi
	.loc 4 122 0
	mov	r1, r2
	.loc 4 119 0
	lsl	r3, r3, #10
	str	r3, [r4, #3224]
	.loc 4 121 0
	str	r3, [r0, #4]
	.loc 4 122 0
	bl	FlashEraseBlocks
.LVL842:
	.loc 4 123 0
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL843:
.L994:
	.loc 4 126 0
	ldrh	r3, [r4, #130]
	add	r3, r3, #1
	strh	r3, [r4, #130]	@ movhi
	.loc 4 127 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	bne	.L995
	.loc 4 130 0
	add	r5, r5, #1
.LVL844:
	.loc 4 129 0
	ldr	r1, [r4, #3224]
	.loc 4 130 0
	uxth	r5, r5
.LVL845:
	.loc 4 129 0
	ldr	r0, .L999+20
	bl	printk
.LVL846:
	.loc 4 131 0
	cmp	r5, #3
	bls	.L993
	.loc 4 132 0
	mov	r2, r5
	ldr	r1, [r4, #3224]
	ldr	r0, .L999+24
	bl	printk
.LVL847:
.L997:
	b	.L997
.LVL848:
.L992:
	.loc 4 92 0 discriminator 3
	ldrh	r2, [r7]
	ldr	r3, [r4, #3228]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	.loc 4 91 0 discriminator 3
	add	r5, r5, #1
	.loc 4 92 0 discriminator 3
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
.LVL849:
	b	.L991
.LVL850:
.L998:
	mov	r7, #1
	b	.L993
.L995:
	.loc 4 139 0
	cmp	r7, #0
	beq	.L998
	.loc 4 146 0
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1000:
	.align	2
.L999:
	.word	.LANCHOR0
	.word	.LANCHOR0+3860
	.word	.LC93
	.word	.LANCHOR0+3220
	.word	-3887
	.word	.LC94
	.word	.LC95
.LFE176:
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
.LFB248:
	.loc 2 2579 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL851:
	mov	ip, sp
.LCFI304:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI305:
	sub	fp, ip, #4
.LCFI306:
	sub	sp, sp, #16
	.loc 2 2579 0
	mov	r5, r0
	.loc 2 2585 0
	ldr	r4, .L1036
	add	r8, r4, #2320
.LVL852:
.L1002:
	ldr	r7, .L1036+4
.LVL853:
	ldrh	r3, [r8]
	ldrh	r2, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1003
	.loc 2 2585 0 is_stmt 0 discriminator 1
	movw	r2, #2585
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL854:
.L1003:
	.loc 2 2587 0 is_stmt 1
	ldr	r3, .L1036+16
	cmp	r5, r3
	movne	r1, #0
.LVL855:
	bne	.L1004
	.loc 2 2588 0
	ldrh	r3, [r8]
	ldr	r1, [r4, #3136]
.LVL856:
	mul	r2, r1, r3
	lsr	r1, r3, #1
	add	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
.LVL857:
	.loc 2 2592 0
	cmp	r1, #0
	.loc 2 2593 0
	subne	r1, r1, #1
.LVL858:
	uxthne	r1, r1
.LVL859:
.L1004:
	.loc 2 2594 0
	ldr	r0, .L1036+20
	bl	List_pop_index_node
.LVL860:
	ldrh	r3, [r8]
	mov	r6, r0
	uxth	r9, r0
	cmp	r3, #0
	bne	.L1005
	.loc 2 2594 0 is_stmt 0 discriminator 1
	movw	r2, #2594
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL861:
.L1005:
	.loc 2 2594 0 discriminator 3
	ldrh	r3, [r8]
	.loc 2 2598 0 is_stmt 1 discriminator 3
	mov	r0, r5
	.loc 2 2594 0 discriminator 3
	sub	r3, r3, #1
	strh	r3, [r8]	@ movhi
	.loc 2 2597 0 discriminator 3
	strh	r9, [r5]	@ movhi
	.loc 2 2598 0 discriminator 3
	bl	make_superblock
.LVL862:
	.loc 2 2599 0 discriminator 3
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1006
	uxth	r0, r6
	.loc 2 2603 0
	ldr	r2, [r4, #2300]
	mvn	r1, #0
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	.loc 2 2604 0
	bl	INSERT_DATA_LIST
.LVL863:
	.loc 2 2605 0
	ldrh	r2, [r7]
	ldrh	r3, [r8]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1002
	.loc 2 2605 0 is_stmt 0 discriminator 1
	movw	r2, #2605
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL864:
	b	.L1002
.L1006:
	.loc 2 2608 0 is_stmt 1
	ldrh	r2, [r7]
	ldrh	r3, [r8]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1008
	.loc 2 2608 0 is_stmt 0 discriminator 1
	mov	r2, #2608
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL865:
.L1008:
	.loc 2 2612 0 is_stmt 1 discriminator 1
	ldr	r0, [r4, #3760]
	mov	r2, #20
	ldrh	r1, [r4, #8]
	mov	r7, #0
.LVL866:
	add	ip, r5, #16
	mov	lr, r7
	mov	r3, r0
	mla	r1, r2, r1, r0
	str	r1, [fp, #-44]
.L1009:
	.loc 2 2611 0 discriminator 1
	ldr	r1, [fp, #-44]
	cmp	r1, r3
	bne	.L1011
	.loc 2 2619 0
	cmp	r7, #0
	bne	.L1012
	.loc 2 2619 0 is_stmt 0 discriminator 1
	movw	r2, #2619
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL867:
.L1012:
	.loc 2 2621 0 is_stmt 1
	ldr	r3, .L1036+24
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L1013
	.loc 2 2621 0 is_stmt 0 discriminator 1
	movw	r2, #2621
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL868:
.L1013:
	.loc 2 2622 0 is_stmt 1
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, #0
	bne	.L1014
	.loc 2 2623 0
	ldr	r1, [r4, #2304]
	lsl	r2, r6, #1
	ldrh	r3, [r1, r2]
	cmp	r3, #0
	.loc 2 2624 0
	ldrhne	r0, [r4, #68]
	.loc 2 2626 0
	moveq	r3, #2
	.loc 2 2624 0
	addne	r3, r3, r0
	.loc 2 2628 0
	mov	r0, r6
	.loc 2 2626 0
	strh	r3, [r1, r2]	@ movhi
	.loc 2 2628 0
	mov	r1, #0
	.loc 2 2627 0
	ldr	r3, [r4, #2524]
	add	r3, r3, #1
	str	r3, [r4, #2524]
	.loc 2 2628 0
	bl	ftl_set_blk_mode
.LVL869:
.L1017:
	.loc 2 2635 0
	ldr	r3, [r4, #2304]
	lsl	r10, r6, #1
	ldr	r2, [r4, #2540]
	.loc 2 2638 0
	ldr	r0, [r4, #2524]
	.loc 2 2635 0
	ldrh	r3, [r3, r10]
	.loc 2 2638 0
	ldrh	r1, [r4, #16]
	.loc 2 2635 0
	cmp	r3, r2
	.loc 2 2638 0
	ldrh	r2, [r4, #68]
	.loc 2 2636 0
	strhi	r3, [r4, #2540]
	.loc 2 2638 0
	ldr	r3, [r4, #2528]
	mla	r0, r0, r2, r3
	bl	__udivsi3
.LVL870:
	.loc 2 2639 0
	ldr	r2, [r4, #3820]
	.loc 2 2642 0
	ldr	r1, [r4, #3760]
	.loc 2 2638 0
	str	r0, [r4, #2532]
	.loc 2 2639 0
	ldr	r3, [r2, #16]
	.loc 2 2642 0
	ldr	r0, .L1036+28
	.loc 2 2639 0
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #20
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #24
.L1019:
	add	r3, r3, #20
	.loc 2 2641 0 discriminator 1
	cmp	r2, r3
	bne	.L1020
	.loc 2 2643 0
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3760]
	bl	FlashEraseBlocks
.LVL871:
	.loc 2 2645 0
	mov	r3, #0
	.loc 2 2646 0
	mov	ip, #20
	.loc 2 2644 0
	mov	r1, r3
.LVL872:
.L1021:
	.loc 2 2645 0 discriminator 1
	uxth	r2, r3
	cmp	r7, r2
	bhi	.L1023
	.loc 2 2656 0
	cmp	r1, #0
	ble	.L1024
	.loc 2 2657 0
	mov	r0, r6
	bl	update_multiplier_value
.LVL873:
	.loc 2 2658 0
	bl	FtlBbmTblFlush
.LVL874:
.L1024:
	.loc 2 2661 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1025
	.loc 2 2663 0
	ldr	r3, [r4, #2300]
	mvn	r2, #0
	.loc 2 2664 0
	mov	r0, r6
	.loc 2 2663 0
	strh	r2, [r3, r10]	@ movhi
	.loc 2 2664 0
	bl	INSERT_DATA_LIST
.LVL875:
	.loc 2 2665 0
	b	.L1002
.L1011:
	.loc 2 2612 0
	str	lr, [r3, #8]
	.loc 2 2614 0
	movw	r10, #65535
	.loc 2 2613 0
	str	lr, [r3, #12]
	add	r3, r3, #20
	.loc 2 2614 0
	ldrh	r1, [ip], #2
	cmp	r1, r10
	.loc 2 2615 0
	mlane	r10, r2, r7, r0
	lslne	r1, r1, #10
	.loc 2 2616 0
	addne	r7, r7, #1
.LVL876:
	uxthne	r7, r7
.LVL877:
	.loc 2 2615 0
	strne	r1, [r10, #4]
	b	.L1009
.L1014:
	.loc 2 2630 0
	ldr	r1, [r4, #2304]
	lsl	r3, r6, #1
.LBB451:
.LBB452:
	mov	r0, r6
.LBE452:
.LBE451:
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	.loc 2 2631 0
	ldr	r3, [r4, #2528]
	add	r3, r3, #1
	str	r3, [r4, #2528]
.LBB454:
.LBB453:
	bl	ftl_set_blk_mode.part.6
.LVL878:
	b	.L1017
.L1020:
.LBE453:
.LBE454:
	.loc 2 2642 0 discriminator 3
	ldr	r1, [r3, #-20]
	and	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1019
.LVL879:
.L1023:
	.loc 2 2646 0
	mul	r2, ip, r3
	ldr	lr, [r4, #3760]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L1022
	.loc 2 2648 0
	ldr	r0, [r0, #4]
	.loc 2 2647 0
	add	r1, r1, #1
	str	ip, [fp, #-56]
	str	r2, [fp, #-52]
	.loc 2 2649 0
	ubfx	r0, r0, #10, #16
	str	r3, [fp, #-48]
	.loc 2 2647 0
	str	r1, [fp, #-44]
	.loc 2 2649 0
	bl	FtlBbmMapBadBlock
.LVL880:
	.loc 2 2650 0
	ldr	r3, [fp, #-48]
	ldr	r2, [fp, #-52]
	.loc 2 2651 0
	ldr	ip, [fp, #-56]
	ldr	r1, [fp, #-44]
.LVL881:
	.loc 2 2650 0
	add	r0, r5, r3, lsl #1
	strh	r2, [r0, #16]	@ movhi
	.loc 2 2651 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r5, #7]
.L1022:
	add	r3, r3, #1
	b	.L1021
.L1025:
	.loc 2 2668 0
	ldrh	r3, [r4, #78]
	.loc 2 2669 0
	strh	r9, [r5]	@ movhi
	.loc 2 2668 0
	smulbb	r3, r3, r2
	.loc 2 2670 0
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	.loc 2 2671 0
	strb	r2, [r5, #6]
	.loc 2 2672 0
	ldr	r2, [r4, #2516]
	.loc 2 2668 0
	uxth	r3, r3
	.loc 2 2673 0
	ldr	r1, [r4, #2300]
.LVL882:
	.loc 2 2668 0
	strh	r3, [r5, #4]	@ movhi
	.loc 2 2672 0
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #2516]
	.loc 2 2673 0
	ldrh	r2, [r5]
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
	.loc 2 2674 0
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1026
	.loc 2 2674 0 is_stmt 0 discriminator 2
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1027
.L1026:
	.loc 2 2674 0 discriminator 3
	movw	r2, #2674
	ldr	r1, .L1036+8
	ldr	r0, .L1036+12
	bl	printk
.LVL883:
.L1027:
	.loc 2 2676 0 is_stmt 1
	mov	r0, #0
	sub	sp, fp, #40
.LCFI307:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL884:
.L1037:
	.align	2
.L1036:
	.word	.LANCHOR0
	.word	.LANCHOR0+2312
	.word	.LANCHOR1+360
	.word	.LC1
	.word	.LANCHOR0+2420
	.word	.LANCHOR0+2316
	.word	.LANCHOR0+2564
	.word	-1024
.LFE248:
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
.LFB267:
	.loc 5 390 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL885:
	mov	ip, sp
.LCFI308:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI309:
	sub	fp, ip, #4
.LCFI310:
	sub	sp, sp, #8
	.loc 5 390 0
	uxth	r3, r0
.LVL886:
	.loc 5 395 0
	ldr	r6, .L1050
	.loc 5 390 0
	str	r3, [fp, #-44]
	.loc 5 395 0
	movw	r3, #3182
.LVL887:
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
.LBB457:
.LBB458:
	.loc 5 398 0
	ldrne	r10, .L1050+4
.LBE458:
.LBE457:
	.loc 5 395 0
	bne	.L1040
.LVL888:
.L1039:
	.loc 5 415 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI311:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL889:
.L1046:
.LCFI312:
.LBB460:
.LBB459:
	.loc 5 397 0
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL890:
	mov	r9, r0
.L1041:
	.loc 5 398 0
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L1045
	add	r7, r7, #1
.L1040:
	.loc 5 396 0
	ldrh	r2, [r6, #8]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1046
	.loc 5 412 0
	bl	FtlGcReFreshBadBlk
.LVL891:
	b	.L1039
.L1045:
	.loc 5 399 0
	uxth	r3, r8
	ldr	r5, .L1050+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L1042
	.loc 5 401 0
	mov	r1, r9
	ldr	r0, .L1050+12
	str	r3, [fp, #-48]
	bl	printk
.LVL892:
	.loc 5 403 0
	mov	r0, r9
	bl	FtlBbmMapBadBlock
.LVL893:
	.loc 5 404 0
	bl	FtlBbmTblFlush
.LVL894:
	ldr	r3, [fp, #-48]
	.loc 5 405 0
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L1043:
	cmp	r4, r2
	bcc	.L1044
	.loc 5 407 0
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L1042:
	add	r8, r8, #1
	b	.L1041
.L1044:
	.loc 5 406 0
	ldrh	r1, [r3, #2]!
	.loc 5 405 0
	add	r4, r4, #1
.LVL895:
	uxth	r4, r4
.LVL896:
	.loc 5 406 0
	strh	r1, [r3, #-2]	@ movhi
	b	.L1043
.L1051:
	.align	2
.L1050:
	.word	.LANCHOR0
	.word	.LANCHOR0+3182
	.word	.LANCHOR0+3184
	.word	.LC96
.LBE459:
.LBE460:
.LFE267:
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
.LFB251:
	.loc 2 2735 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL897:
	mov	ip, sp
.LCFI313:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI314:
	sub	fp, ip, #4
.LCFI315:
	.loc 2 2736 0
	ldr	r3, .L1062
	.loc 2 2735 0
	uxth	r4, r0
.LVL898:
	.loc 2 2736 0
	ldr	r1, [r3, #2300]
	lsl	r2, r4, #1
	mov	r5, r3
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	bne	.L1053
	.loc 2 2737 0
	movw	r2, #2564
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	.loc 2 2739 0
	mvneq	r3, #0
	strheq	r3, [r5, r2]	@ movhi
	.loc 2 2737 0
	beq	.L1055
	.loc 2 2740 0
	movw	r2, #2324
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	.loc 2 2740 0 is_stmt 0 discriminator 1
	movw	r2, #2372
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	.loc 2 2740 0 discriminator 2
	movw	r2, #2420
	ldrh	r3, [r3, r2]
	cmp	r3, r4
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
.L1055:
.LBB463:
.LBB464:
	.loc 2 2744 0 is_stmt 1
	mov	r1, r4
	ldr	r0, .L1062+4
	bl	List_remove_node
.LVL899:
	movw	r3, #2312
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1057
	movw	r2, #2744
	ldr	r1, .L1062+8
	ldr	r0, .L1062+12
	bl	printk
.LVL900:
.L1057:
	movw	r6, #2312
	.loc 2 2745 0
	mov	r0, r4
	.loc 2 2744 0
	ldrh	r3, [r5, r6]
	sub	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	.loc 2 2745 0
	bl	free_data_superblock
.LVL901:
	.loc 2 2746 0
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
.LVL902:
	.loc 2 2747 0
	ldr	r3, .L1062+16
	ldrh	r2, [r5, r6]
	ldrh	r3, [r3]
	add	r3, r3, r2
	ldrh	r2, [r5, #16]
	cmp	r3, r2
	ble	.L1061
	movw	r2, #2747
	ldr	r1, .L1062+8
	ldr	r0, .L1062+12
	bl	printk
.LVL903:
.L1061:
	mov	r0, #1
.LBE464:
.LBE463:
	.loc 2 2753 0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1053:
	.loc 2 2750 0
	mov	r0, r4
	bl	List_update_data_list
.LVL904:
	.loc 2 2752 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1063:
	.align	2
.L1062:
	.word	.LANCHOR0
	.word	.LANCHOR0+2296
	.word	.LANCHOR1+385
	.word	.LC1
	.word	.LANCHOR0+2320
.LFE251:
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.syntax unified
	.arm
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
.LFB252:
	.loc 2 2756 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL905:
	mov	ip, sp
.LCFI316:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI317:
	sub	fp, ip, #4
.LCFI318:
	.loc 2 2756 0
	uxth	r5, r0
.LVL906:
	.loc 2 2759 0
	movw	r3, #65535
	cmp	r5, r3
	ldr	r6, .L1076
	beq	.L1065
	.loc 2 2760 0
	ldr	r3, [r6, #2300]
	lsl	r4, r5, #1
	ldrh	r2, [r3, r4]
	cmp	r2, #0
	.loc 2 2765 0
	subne	r2, r2, #1
	strhne	r2, [r3, r4]	@ movhi
	.loc 2 2760 0
	bne	.L1065
	.loc 2 2761 0
	mov	r1, r5
	ldr	r0, .L1076+4
	bl	printk
.LVL907:
	.loc 2 2762 0
	ldr	r3, [r6, #2300]
	ldrh	r4, [r3, r4]
	cmp	r4, #0
	beq	.L1067
.L1074:
	.loc 2 2771 0
	mov	r4, #0
.L1064:
	.loc 2 2781 0
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1067:
	.loc 2 2762 0 discriminator 1
	movw	r2, #2762
.L1075:
	.loc 2 2778 0 discriminator 2
	ldr	r1, .L1076+8
	ldr	r0, .L1076+12
	bl	printk
.LVL908:
	b	.L1064
.L1065:
	.loc 2 2769 0
	ldr	r7, .L1076+16
	movw	r3, #65535
	ldrh	r0, [r7]
	cmp	r0, r3
	.loc 2 2770 0
	strheq	r5, [r7]	@ movhi
	beq	.L1074
.L1069:
	.loc 2 2772 0
	cmp	r5, r0
	beq	.L1074
	.loc 2 2775 0
	bl	update_vpc_list
.LVL909:
	.loc 2 2778 0
	ldr	r2, [r6, #2292]
	.loc 2 2775 0
	adds	r4, r0, #0
	.loc 2 2778 0
	ldr	r3, [r6, #2296]
	.loc 2 2775 0
	movne	r4, #1
	.loc 2 2778 0
	ldr	r1, [r6, #2300]
	.loc 2 2777 0
	strh	r5, [r7]	@ movhi
	.loc 2 2778 0
	sub	r3, r3, r2
	asr	r3, r3, #3
	uxth	r2, r3
	uxth	r3, r3
	lsl	r2, r2, #1
	cmp	r3, r5
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L1064
	.loc 2 2778 0 is_stmt 0 discriminator 2
	movw	r2, #2778
	b	.L1075
.L1077:
	.align	2
.L1076:
	.word	.LANCHOR0
	.word	.LC97
	.word	.LANCHOR1+401
	.word	.LC1
	.word	.LANCHOR0+3952
.LFE252:
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
.LFB218:
	.loc 2 996 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 28
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI319:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI320:
	sub	fp, ip, #4
.LCFI321:
	sub	sp, sp, #28
	.loc 2 997 0
	ldr	r6, .L1096
	ldr	r4, .L1096+4
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L1079
	.loc 2 997 0 is_stmt 0 discriminator 1
	ldrb	r3, [r4, #2332]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1079
	.loc 2 998 0 is_stmt 1
	ldrb	r1, [r4, #2331]	@ zero_extendqisi2
	ldrh	r3, [r4, #78]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1079
.LBB468:
.LBB469:
	.loc 2 1005 0
	ldrb	r8, [r4, #2334]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1078
	.loc 2 1000 0
	ldr	r7, [r4, #2480]
	.loc 2 1007 0
	mov	r2, r8
	sub	r1, fp, #64
	.loc 2 1002 0
	ldrh	r9, [r4, #8]
	.loc 2 1000 0
	sub	r7, r7, #1
.LVL910:
	.loc 2 1007 0
	mov	r0, r7
	bl	log2phys
.LVL911:
	.loc 2 1010 0
	ldr	r3, [fp, #-64]
	.loc 2 1013 0
	ldr	r5, [r4, #3244]
	.loc 2 1012 0
	ldr	r0, [r4, #3240]
	.loc 2 1016 0
	cmn	r3, #1
	.loc 2 1010 0
	str	r3, [fp, #-56]
	.loc 2 1011 0
	str	r7, [fp, #-44]
	.loc 2 1012 0
	str	r0, [fp, #-52]
	.loc 2 1013 0
	str	r5, [fp, #-48]
	.loc 2 1015 0
	str	r8, [r5, #4]
	.loc 2 1016 0
	beq	.L1081
	.loc 2 1018 0
	mov	r2, r8
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.LVL912:
.L1082:
	.loc 2 1035 0
	ldr	ip, .L1096
	.loc 2 1024 0
	mov	r8, #0
	ldr	r3, .L1096+8
	.loc 2 1002 0
	lsl	r9, r9, #2
.LVL913:
	.loc 2 1036 0
	mov	r10, r8
	.loc 2 1024 0
	strh	r3, [r5]	@ movhi
.L1083:
	.loc 2 1025 0
	cmp	r9, r8
	bne	.L1087
.L1084:
	.loc 2 1041 0
	mov	r3, #1
.L1095:
.LBE469:
.LBE468:
	.loc 2 1047 0
	strb	r3, [r4, #2334]
.L1078:
	.loc 2 1049 0
	sub	sp, fp, #40
.LCFI322:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL914:
.L1081:
.LCFI323:
.LBB471:
.LBB470:
	.loc 2 1022 0
	ldrh	r2, [r4, #86]
	mov	r1, #255
	bl	ftl_memset
.LVL915:
	b	.L1082
.LVL916:
.L1087:
	.loc 2 1027 0
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L1084
	.loc 2 1032 0
	ldr	r3, [fp, #-56]
	.loc 2 1035 0
	mov	r0, ip
	.loc 2 1031 0
	str	r7, [r5, #8]
	add	r8, r8, #1
	.loc 2 1035 0
	str	ip, [fp, #-68]
	.loc 2 1032 0
	str	r3, [r5, #12]
	.loc 2 1033 0
	ldrh	r3, [r6]
	strh	r3, [r5, #2]	@ movhi
	.loc 2 1035 0
	bl	get_new_active_ppa
.LVL917:
	.loc 2 1036 0
	ldr	r3, [r4, #2520]
	.loc 2 1037 0
	mov	r1, #1
	.loc 2 1035 0
	str	r0, [fp, #-56]
	.loc 2 1037 0
	sub	r0, fp, #60
	.loc 2 1036 0
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r10
	str	r3, [r4, #2520]
	.loc 2 1037 0
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL918:
	.loc 2 1039 0
	ldrh	r0, [r6]
	bl	decrement_vpc_count
.LVL919:
	ldr	ip, [fp, #-68]
	b	.L1083
.LVL920:
.L1079:
.LBE470:
.LBE471:
	.loc 2 1047 0
	mov	r3, #0
	b	.L1095
.L1097:
	.align	2
.L1096:
	.word	.LANCHOR0+2324
	.word	.LANCHOR0
	.word	-3947
.LFE218:
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	l2p_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
.LFB211:
	.loc 2 789 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI324:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI325:
	sub	fp, ip, #4
.LCFI326:
	.loc 2 795 0
	ldr	r5, .L1102
	mov	r4, #0
	.loc 2 796 0
	mov	r6, #12
	.loc 2 793 0
	bl	FtlWriteDump_data
.LVL921:
.L1099:
	.loc 2 795 0 discriminator 1
	ldrh	r2, [r5, #114]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1101
	.loc 2 799 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1101:
	.loc 2 796 0
	ldr	r3, [r5, #2468]
	uxth	r0, r4
	mla	r3, r6, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1100
	.loc 2 797 0
	bl	flush_l2p_region
.LVL922:
.L1100:
	add	r4, r4, #1
	b	.L1099
.L1103:
	.align	2
.L1102:
	.word	.LANCHOR0
.LFE211:
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
.LFB231:
	.loc 2 1705 0
	@ args = 0, pretend = 0, frame = 44
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL923:
	mov	ip, sp
.LCFI327:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI328:
	sub	fp, ip, #4
.LCFI329:
	sub	sp, sp, #44
	.loc 2 1719 0
	movw	r2, #65535
	ldrh	r3, [r0]
	.loc 2 1705 0
	mov	r4, r0
	.loc 2 1719 0
	cmp	r3, r2
	beq	.L1225
	.loc 2 1724 0
	ldrh	r3, [r0, #2]
.LVL924:
	.loc 2 1730 0
	ldr	r6, .L1232
	.loc 2 1724 0
	str	r3, [fp, #-64]
	.loc 2 1725 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
.LVL925:
	.loc 2 1730 0
	ldr	r1, [fp, #-64]
.LVL926:
	.loc 2 1725 0
	str	r3, [fp, #-80]
	.loc 2 1730 0
	ldrh	r3, [r6, #78]
.LVL927:
	cmp	r3, r1
	mov	r3, #0
	.loc 2 1731 0
	strheq	r3, [r0, #4]	@ movhi
	.loc 2 1738 0
	ldrhne	r0, [r0, #16]
.LVL928:
	.loc 2 1730 0
	bne	.L1108
.LVL929:
.L1230:
	.loc 2 2038 0
	strb	r3, [r4, #6]
.L1225:
	.loc 2 2059 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI330:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL930:
.L1109:
.LCFI331:
	.loc 2 1741 0
	uxth	r1, r3
.LVL931:
	add	r1, r4, r1, lsl #1
	ldrh	r0, [r1, #16]
.LVL932:
.L1108:
	.loc 2 1739 0
	cmp	r0, r2
	uxth	r7, r3
	add	r3, r3, #1
	beq	.L1109
	.loc 2 1746 0
	mov	r1, #1
.LVL933:
	bl	FtlGetLastWrittenPage
.LVL934:
	.loc 2 1754 0
	cmn	r0, #1
	.loc 2 1746 0
	mov	r5, r0
.LVL935:
	.loc 2 1754 0
	beq	.L1110
	.loc 2 1766 0
	ldrh	r3, [r6, #8]
	.loc 2 1771 0
	mov	r2, #0
	.loc 2 1769 0
	ldr	r0, [r6, #3792]
	.loc 2 1768 0
	movw	r8, #65535
	.loc 2 1771 0
	ldr	lr, [r6, #3152]
	.loc 2 1769 0
	mov	r9, #20
	.loc 2 1766 0
	str	r3, [fp, #-56]
	.loc 2 1770 0
	ldr	r3, [r6, #3148]
	.loc 2 1771 0
	ldrh	r7, [r6, #88]
	.loc 2 1770 0
	str	r3, [fp, #-60]
	ldrh	r3, [r6, #86]
	.loc 2 1771 0
	mov	r6, r2
	.loc 2 1770 0
	str	r3, [fp, #-68]
	add	r3, r4, #16
	.loc 2 1771 0
	mov	ip, r3
	str	r3, [fp, #-76]
.L1111:
	.loc 2 1766 0 discriminator 1
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1115
	.loc 2 1777 0
	ldr	r7, .L1232
	.loc 2 1776 0
	mov	r2, #0
	mov	r1, r6
	bl	FlashReadPages
.LVL936:
	.loc 2 1777 0
	ldr	r3, [r7, #2520]
	.loc 2 1715 0
	movw	r2, #65535
	.loc 2 1779 0
	ldr	r9, [r7, #3792]
	.loc 2 1782 0
	mov	r10, r7
	.loc 2 1715 0
	str	r2, [fp, #-72]
	.loc 2 1791 0
	uxth	r2, r5
	.loc 2 1777 0
	sub	r3, r3, #1
.LVL937:
	str	r3, [fp, #-56]
	mov	r8, r9
	.loc 2 1778 0
	mov	r3, #0
.LVL938:
	.loc 2 1791 0
	str	r2, [fp, #-60]
.L1116:
	uxth	r2, r3
.LVL939:
	.loc 2 1778 0 discriminator 1
	cmp	r6, r2
	bhi	.L1121
	.loc 2 1796 0
	addeq	r3, r5, #1
	uxtheq	r3, r3
.LVL940:
	streq	r3, [fp, #-60]
	.loc 2 1794 0
	bne	.L1119
.L1226:
	.loc 2 1802 0
	ldr	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
.LVL941:
	.loc 2 1807 0
	ldrh	r3, [r7, #78]
	.loc 2 1813 0
	sub	r1, fp, #68
	.loc 2 1807 0
	ldr	r2, [fp, #-60]
.LVL942:
	.loc 2 1802 0
	str	r0, [fp, #-68]
	.loc 2 1813 0
	ldr	r0, [fp, #-80]
.LVL943:
	.loc 2 1807 0
	cmp	r3, r2
	.loc 2 1808 0
	ldrheq	r3, [fp, #-60]
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 1809 0
	moveq	r3, #0
	strbeq	r3, [r4, #6]
	.loc 2 1810 0
	strheq	r3, [r4, #4]	@ movhi
	.loc 2 1813 0
	ldm	r1, {r1, r2, r3}
.LVL944:
	cmp	r3, r2
	cmpeq	r1, r0
	.loc 2 1814 0
	moveq	r2, r1
.LVL945:
	moveq	r1, r3
.LVL946:
	.loc 2 1813 0
	beq	.L1231
	.loc 2 1819 0
	ldr	r2, [fp, #-72]
	movw	r3, #65535
.LVL947:
	cmp	r2, r3
	bne	.L1125
	.loc 2 1819 0 is_stmt 0 discriminator 1
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1126
.L1125:
.LBB472:
	.loc 2 1824 0 is_stmt 1
	ldr	r3, [r7, #3964]
	.loc 2 1823 0
	uxth	r8, r5
	.loc 2 1827 0
	uxth	r5, r5
.LVL948:
	.loc 2 1824 0
	cmn	r3, #1
	.loc 2 1825 0
	ldreq	r3, [fp, #-56]
.LVL949:
	streq	r3, [r7, #3964]
	.loc 2 1827 0
	ldr	r3, [fp, #-64]
.LVL950:
	ldr	r7, .L1232
	add	r3, r3, #7
.LVL951:
	cmp	r5, r3
	.loc 2 1828 0
	subgt	r5, r8, #7
	ldrle	r5, [fp, #-64]
	uxthgt	r5, r5
.L1129:
	.loc 2 1829 0 discriminator 1
	cmp	r5, r8
	bhi	.L1136
	.loc 2 1834 0
	mov	r3, #0
	.loc 2 1831 0
	ldrh	r9, [r7, #8]
	.loc 2 1834 0
	ldr	r0, [r7, #3792]
.LVL952:
	mov	r6, r3
	ldr	r1, [fp, #-76]
	.loc 2 1833 0
	movw	lr, #65535
	.loc 2 1834 0
	mov	ip, #20
	b	.L1137
.LVL953:
.L1110:
.LBE472:
	.loc 2 1755 0
	ldr	r3, [fp, #-64]
.LVL954:
	cmp	r3, #0
	beq	.L1112
	.loc 2 1755 0 is_stmt 0 discriminator 1
	movw	r2, #1755
	ldr	r1, .L1232+4
	ldr	r0, .L1232+8
	bl	printk
.LVL955:
.L1112:
	.loc 2 1756 0 is_stmt 1
	ldr	r3, [fp, #-80]
.LVL956:
	cmp	r3, #0
	cmpne	r7, r3
	beq	.L1113
	.loc 2 1756 0 is_stmt 0 discriminator 1
	movw	r2, #1756
	ldr	r1, .L1232+4
	ldr	r0, .L1232+8
	bl	printk
.LVL957:
.L1113:
	.loc 2 1757 0 is_stmt 1
	mov	r3, #0
.LVL958:
	strh	r3, [r4, #2]	@ movhi
	b	.L1230
.L1115:
	.loc 2 1767 0
	ldrh	r3, [ip], #2
	.loc 2 1768 0
	cmp	r3, r8
	beq	.L1114
	.loc 2 1769 0
	mla	r1, r9, r6, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	.loc 2 1770 0
	ldr	r3, [fp, #-68]
	mul	r3, r3, r6
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [fp, #-60]
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r1, #8]
	.loc 2 1771 0
	mul	r3, r7, r6
	.loc 2 1772 0
	add	r6, r6, #1
	uxth	r6, r6
	.loc 2 1771 0
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L1114:
	add	r2, r2, #1
	b	.L1111
.LVL959:
.L1121:
	.loc 2 1779 0
	ldr	r2, [r8]
.LVL960:
	cmp	r2, #0
	bne	.L1117
	.loc 2 1780 0
	ldr	ip, [r8, #12]
	.loc 2 1781 0
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1118
	.loc 2 1782 0
	ldr	r1, [r10, #2520]
	mov	r0, r2
	str	ip, [fp, #-68]
	bl	ftl_cmp_data_ver
.LVL961:
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	.loc 2 1784 0
	addne	r2, r2, #1
	strne	r2, [r10, #2520]
.L1118:
	.loc 2 1787 0
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1120
.LVL962:
.L1119:
	.loc 2 1801 0
	uxth	r2, r5
.LVL963:
	.loc 2 1802 0
	uxth	r3, r3
	.loc 2 1801 0
	str	r2, [fp, #-60]
	.loc 2 1802 0
	mov	r2, #20
.LVL964:
	mla	r9, r2, r3, r9
	b	.L1226
.L1117:
	.loc 2 1791 0
	ldr	r2, [fp, #-60]
	str	r2, [fp, #-72]
.L1120:
	add	r3, r3, #1
	add	r8, r8, #20
	b	.L1116
.LVL965:
.L1131:
.LBB473:
	.loc 2 1832 0
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	.loc 2 1833 0
	cmp	r2, lr
	.loc 2 1834 0
	mlane	r10, ip, r6, r0
	.loc 2 1835 0
	addne	r6, r6, #1
	.loc 2 1834 0
	orrne	r2, r5, r2, lsl #10
	.loc 2 1835 0
	uxthne	r6, r6
	.loc 2 1834 0
	strne	r2, [r10, #4]
.L1137:
	.loc 2 1831 0 discriminator 1
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L1131
	.loc 2 1838 0
	mov	r1, r6
	mov	r2, #0
	bl	FlashReadPages
.LVL966:
	.loc 2 1840 0
	ldr	r3, [r7, #3792]
	mov	r2, #20
	.loc 2 1842 0
	movw	r1, #65535
	mla	r6, r2, r6, r3
.L1132:
	.loc 2 1839 0 discriminator 1
	cmp	r6, r3
	.loc 2 1829 0
	addeq	r5, r5, #1
	uxtheq	r5, r5
	beq	.L1129
.L1135:
	.loc 2 1840 0
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1126
	.loc 2 1841 0
	ldr	r2, [r3, #12]
	.loc 2 1842 0
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1134
	.loc 2 1844 0
	ldr	r2, [r2, #4]
	cmn	r2, #1
	.loc 2 1846 0
	strne	r2, [r7, #3964]
.L1134:
	add	r3, r3, #20
	b	.L1132
.LVL967:
.L1136:
	.loc 2 1858 0
	mvn	r3, #0
	str	r3, [r7, #3964]
.LVL968:
.L1126:
.LBE473:
	.loc 2 1863 0
	ldr	r5, .L1232+12
.LVL969:
	mov	r3, #1
	mov	r0, r5
.LVL970:
	.loc 2 1870 0
	sub	r5, r5, #3968
	.loc 2 1863 0
	strh	r3, [r0], #-104	@ movhi
	.loc 2 1864 0
	bl	FtlMapBlkWriteDump_data
.LVL971:
	ldr	r9, [fp, #-64]
.LVL972:
.L1138:
	.loc 2 1870 0
	ldrh	ip, [r5, #8]
	mov	r3, #0
	.loc 2 1873 0
	ldr	r0, [r5, #3792]
	.loc 2 1869 0
	mov	r10, r3
	.loc 2 1870 0
	ldr	r1, [fp, #-76]
	.loc 2 1872 0
	movw	lr, #65535
	.loc 2 1873 0
	mov	r6, #20
.LVL973:
.L1139:
	.loc 2 1870 0 discriminator 1
	uxth	r2, r3
.LVL974:
	cmp	ip, r2
	bhi	.L1141
	.loc 2 1877 0
	mov	r2, #0
	mov	r1, r10
	bl	FlashReadPages
.LVL975:
	.loc 2 1878 0
	mov	r3, #0
.LVL976:
.L1229:
	str	r3, [fp, #-72]
	.loc 2 1878 0 is_stmt 0 discriminator 2
	ldrh	r3, [fp, #-72]
	cmp	r10, r3
	bhi	.L1169
	.loc 2 2031 0 is_stmt 1
	add	r9, r9, #1
	.loc 2 2032 0
	ldrh	r3, [r5, #78]
	.loc 2 2031 0
	uxth	r9, r9
	.loc 2 2032 0
	cmp	r3, r9
	bne	.L1138
	.loc 2 2035 0
	ldrh	r2, [r5, #8]
.LVL977:
	.loc 2 2037 0
	movw	r0, #65535
	.loc 2 2034 0
	mov	r3, #0
	.loc 2 2033 0
	strh	r9, [r4, #2]	@ movhi
	.loc 2 2034 0
	strh	r3, [r4, #4]	@ movhi
.L1170:
	.loc 2 2035 0 discriminator 1
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1225
	.loc 2 2036 0
	ldr	r1, [fp, #-76]
	ldrh	ip, [r1], #2
	.loc 2 2037 0
	cmp	ip, r0
	.loc 2 2036 0
	str	r1, [fp, #-76]
	add	r1, r3, #1
	.loc 2 2037 0
	bne	.L1230
	mov	r3, r1
	b	.L1170
.LVL978:
.L1141:
	.loc 2 1871 0
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	.loc 2 1872 0
	cmp	r2, lr
	.loc 2 1873 0
	mlane	r7, r6, r10, r0
	orrne	r2, r9, r2, lsl #10
	.loc 2 1874 0
	addne	r10, r10, #1
	uxthne	r10, r10
	.loc 2 1873 0
	strne	r2, [r7, #4]
	b	.L1139
.LVL979:
.L1169:
	ldr	r3, [fp, #-72]
	mov	r7, #20
	.loc 2 1879 0
	ldr	r2, [r5, #3792]
.LVL980:
	mul	r7, r7, r3
	str	r2, [fp, #-84]
	add	r8, r2, r7
	ldr	r6, [r8, #4]
.LVL981:
	.loc 2 1880 0
	ubfx	r0, r6, #10, #16
	.loc 2 1879 0
	str	r6, [fp, #-44]
	.loc 2 1880 0
	bl	P2V_plane
.LVL982:
	.loc 2 1882 0
	ldr	r3, [fp, #-64]
.LVL983:
	cmp	r9, r3
	bcc	.L1143
	.loc 2 1882 0 is_stmt 0 discriminator 1
	ldr	r2, [fp, #-80]
.LVL984:
	.loc 2 1883 0 is_stmt 1 discriminator 1
	moveq	r3, #1
.LVL985:
	movne	r3, #0
	.loc 2 1882 0 discriminator 1
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1143
	.loc 2 1886 0
	ldr	r3, [fp, #-60]
.LVL986:
	ldr	r2, [fp, #-68]
.LVL987:
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1144
	.loc 2 1890 0
	ldr	r2, [fp, #-84]
.LVL988:
	ldr	r3, [r2, r7]
.LVL989:
	cmn	r3, #1
	beq	.L1145
	.loc 2 1891 0
	ldr	r8, [r8, #12]
	.loc 2 1892 0
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1146
.LVL990:
.L1153:
.LBB474:
	.loc 2 1925 0
	ldrh	r0, [r4]
.LVL991:
.L1228:
.LBE474:
	.loc 2 2009 0
	bl	decrement_vpc_count
.LVL992:
.L1143:
	ldr	r3, [fp, #-72]
.LVL993:
	add	r3, r3, #1
	b	.L1229
.LVL994:
.L1146:
	.loc 2 1898 0
	ldr	r3, [r8, #4]
.LVL995:
	.loc 2 1899 0
	cmn	r3, #1
	.loc 2 1898 0
	str	r3, [fp, #-56]
	.loc 2 1899 0
	beq	.L1147
	.loc 2 1900 0 discriminator 1
	mov	r0, r3
	ldr	r1, [r5, #2520]
	bl	ftl_cmp_data_ver
.LVL996:
	.loc 2 1899 0 discriminator 1
	cmp	r0, #0
	.loc 2 1902 0 discriminator 1
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
.LVL997:
	strne	r3, [r5, #2520]
.LVL998:
.L1147:
	.loc 2 1905 0
	ldrh	r2, [r8]
	movw	r3, #61589
.LVL999:
	cmp	r2, r3
	beq	.L1148
	.loc 2 1905 0 is_stmt 0 discriminator 1
	movw	r2, #1905
	ldr	r1, .L1232+4
	ldr	r0, .L1232+8
	bl	printk
.LVL1000:
.L1148:
	.loc 2 1906 0 is_stmt 1
	ldr	r6, [r8, #8]
.LVL1001:
	.loc 2 1908 0
	sub	r1, fp, #48
	.loc 2 1907 0
	ldr	r3, [r8, #12]
	.loc 2 1908 0
	mov	r2, #0
	mov	r0, r6
	.loc 2 1907 0
	str	r3, [fp, #-52]
	.loc 2 1908 0
	bl	log2phys
.LVL1002:
	.loc 2 1909 0
	ldr	r1, [r5, #3964]
	cmn	r1, #1
	beq	.L1149
	.loc 2 1910 0 discriminator 1
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
.LVL1003:
	.loc 2 1909 0 discriminator 1
	cmp	r0, #0
	beq	.L1149
.LBB475:
	.loc 2 1914 0
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1150
	.loc 2 1915 0
	ldr	r0, [r5, #3792]
	.loc 2 1917 0
	mov	r1, #1
	.loc 2 1915 0
	add	r0, r0, r7
	.loc 2 1916 0
	str	r2, [r0, #4]
	.loc 2 1917 0
	mov	r2, #0
	.loc 2 1915 0
	ldr	r8, [r0, #12]
	.loc 2 1917 0
	bl	FlashReadPages
.LVL1004:
	.loc 2 1930 0
	ldr	r2, [r5, #3792]
	ldr	r1, [r2, r7]
	add	r3, r2, r7
	cmn	r1, #1
	bne	.L1151
.LVL1005:
.L1152:
	.loc 2 1962 0
	mvn	r3, #0
.LVL1006:
	.loc 2 1963 0
	ldrh	r0, [r4]
	.loc 2 1962 0
	str	r3, [fp, #-52]
	.loc 2 1963 0
	bl	decrement_vpc_count
.LVL1007:
.L1160:
.LBE475:
	.loc 2 2003 0
	ldr	r7, [fp, #-52]
	cmn	r7, #1
	beq	.L1143
.L1173:
	.loc 2 2005 0
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
.LVL1008:
	.loc 2 2006 0
	ldrh	r3, [r5, #16]
	.loc 2 2005 0
	mov	r6, r0
.LVL1009:
	.loc 2 2006 0
	cmp	r3, r0
	bhi	.L1165
	.loc 2 2006 0 is_stmt 0 discriminator 1
	movw	r2, #2006
	ldr	r1, .L1232+4
	ldr	r0, .L1232+8
	bl	printk
.LVL1010:
.L1165:
	.loc 2 2007 0 is_stmt 1
	ldr	r2, [r5, #2300]
	lsl	r3, r6, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1166
	.loc 2 2009 0
	mov	r0, r6
	b	.L1228
.LVL1011:
.L1150:
.LBB476:
	.loc 2 1921 0
	ldr	r3, [fp, #-44]
.LVL1012:
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1153
	.loc 2 1922 0
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r6
	bl	log2phys
.LVL1013:
	b	.L1153
.L1151:
	.loc 2 1930 0 discriminator 1
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1152
	.loc 2 1918 0 discriminator 2
	ldr	r1, [r8, #4]
.LVL1014:
	.loc 2 1931 0 discriminator 2
	ldr	r0, [r5, #3964]
	.loc 2 1918 0 discriminator 2
	str	r1, [fp, #-84]
	.loc 2 1931 0 discriminator 2
	bl	ftl_cmp_data_ver
.LVL1015:
	.loc 2 1930 0 discriminator 2
	cmp	r0, #0
	beq	.L1152
	.loc 2 1932 0
	ldr	r1, [fp, #-48]
.LVL1016:
	ldr	r0, [fp, #-44]
.LVL1017:
	cmp	r1, r0
	bne	.L1155
.LVL1018:
.L1158:
	.loc 2 1958 0
	ldr	r1, [fp, #-52]
	mov	r0, r6
	bl	FtlReUsePrevPpa
.LVL1019:
	b	.L1152
.L1155:
	.loc 2 1935 0
	ldr	r0, [fp, #-52]
.LVL1020:
	cmp	r1, r0
	beq	.L1152
	.loc 2 1936 0
	cmn	r1, #1
	.loc 2 1941 0
	streq	r1, [r2, r7]
	.loc 2 1936 0
	beq	.L1157
	.loc 2 1938 0
	str	r1, [r3, #4]
	.loc 2 1939 0
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	.loc 2 1937 0
	ldr	r8, [r3, #12]
	.loc 2 1939 0
	bl	FlashReadPages
.LVL1021:
.L1157:
	.loc 2 1944 0
	ldr	r2, [r5, #3792]
	ldr	r2, [r2, r7]
	cmn	r2, #1
	beq	.L1158
	.loc 2 1945 0
	ldr	r3, [r8, #4]
.LVL1022:
	.loc 2 1946 0
	ldr	r0, [r5, #3964]
	mov	r1, r3
	bl	ftl_cmp_data_ver
.LVL1023:
	cmp	r0, #0
	beq	.L1158
	.loc 2 1947 0
	mov	r1, r3
	ldr	r0, [fp, #-84]
	bl	ftl_cmp_data_ver
.LVL1024:
	cmp	r0, #0
	beq	.L1152
	b	.L1158
.LVL1025:
.L1149:
.LBE476:
	.loc 2 1964 0
	ldr	r3, [fp, #-44]
.LVL1026:
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1160
	.loc 2 1966 0
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r6
	bl	log2phys
.LVL1027:
	.loc 2 1967 0
	ldr	r7, [fp, #-48]
	cmn	r7, #1
	beq	.L1160
	.loc 2 1967 0 is_stmt 0 discriminator 1
	ldr	r3, [fp, #-52]
	cmp	r7, r3
	beq	.L1173
	.loc 2 1968 0 is_stmt 1
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
.LVL1028:
	.loc 2 1970 0
	ldr	r3, .L1232+16
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L1164
	.loc 2 1970 0 is_stmt 0 discriminator 1
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L1164
	.loc 2 1971 0 is_stmt 1
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L1160
.L1164:
.LBB477:
	.loc 2 1976 0
	ldr	r0, [r5, #3792]
	.loc 2 1979 0
	mov	r2, #0
	mov	r1, #1
	.loc 2 1978 0
	str	r7, [r0, #4]
	.loc 2 1976 0
	ldr	r8, [r0, #12]
	.loc 2 1979 0
	bl	FlashReadPages
.LVL1029:
	.loc 2 1982 0
	ldr	r3, [r5, #3792]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1160
	.loc 2 1983 0
	ldr	r1, [r8, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
.LVL1030:
	cmp	r0, #0
	bne	.L1160
	.loc 2 1990 0
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r6
	bl	log2phys
.LVL1031:
	b	.L1160
.LVL1032:
.L1166:
.LBE477:
	.loc 2 2011 0
	mov	r1, r6
	ldr	r0, .L1232+20
	bl	printk
.LVL1033:
	b	.L1143
.LVL1034:
.L1145:
	.loc 2 2018 0
	ldr	r3, [r5, #3972]
	cmp	r3, #31
	.loc 2 2019 0
	addls	r2, r5, r3, lsl #2
	.loc 2 2020 0
	addls	r3, r3, #1
	strls	r3, [r5, #3972]
	.loc 2 2019 0
	strls	r6, [r2, #3976]
	.loc 2 2022 0
	ldrh	r0, [r4]
	bl	decrement_vpc_count
.LVL1035:
	.loc 2 2024 0
	ldr	r3, [r5, #3964]
	cmn	r3, #1
	.loc 2 2025 0
	ldreq	r3, [fp, #-56]
.LVL1036:
	.loc 2 2024 0
	beq	.L1227
	.loc 2 2026 0
	ldr	r2, [fp, #-56]
.LVL1037:
	cmp	r2, r3
	bcs	.L1143
	.loc 2 2027 0
	mov	r3, r2
.LVL1038:
.L1227:
	str	r3, [r5, #3964]
	b	.L1143
.LVL1039:
.L1144:
	.loc 2 2051 0
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
.LVL1040:
	.loc 2 2053 0
	ldr	r2, [fp, #-68]
.LVL1041:
	ldr	r1, [fp, #-60]
	.loc 2 2051 0
	strb	r3, [r4, #6]
	.loc 2 2052 0
	ldrh	r3, [fp, #-60]
	strh	r3, [r4, #2]	@ movhi
.LVL1042:
.L1231:
	.loc 2 2053 0
	mov	r0, r4
.LVL1043:
	bl	ftl_sb_update_avl_pages
.LVL1044:
	b	.L1225
.L1233:
	.align	2
.L1232:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3968
	.word	.LANCHOR0+2324
	.word	.LC98
.LFE231:
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
.LFB238:
	.loc 2 2205 0
	@ args = 0, pretend = 0, frame = 20
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1045:
	mov	ip, sp
.LCFI332:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI333:
	sub	fp, ip, #4
.LCFI334:
	sub	sp, sp, #20
	.loc 2 2211 0
	mvn	r3, #0
	str	r3, [fp, #-36]
	.loc 2 2219 0
	mov	r7, #0
	.loc 2 2212 0
	ldr	r3, .L1245
	.loc 2 2220 0
	movw	r1, #61589
	.loc 2 2205 0
	mov	r4, r0
	.loc 2 2223 0
	mov	r6, #7
	.loc 2 2212 0
	ldr	r2, [r3, #3240]
	mov	r8, r3
	.loc 2 2213 0
	ldr	r5, [r3, #3244]
	.loc 2 2212 0
	str	r2, [fp, #-44]
	.loc 2 2216 0
	mvn	r2, #2
	.loc 2 2213 0
	str	r5, [fp, #-40]
	.loc 2 2216 0
	str	r2, [r5, #8]
	.loc 2 2217 0
	mvn	r2, #1
	str	r2, [r5, #12]
	.loc 2 2218 0
	ldrh	r2, [r0]
	.loc 2 2219 0
	strh	r7, [r5]	@ movhi
	.loc 2 2218 0
	strh	r2, [r5, #2]	@ movhi
	.loc 2 2220 0
	ldr	r2, [r3, #3240]
	str	r1, [r2]
	.loc 2 2221 0
	ldr	r1, .L1245+4
	ldr	r2, [r3, #3240]
	str	r1, [r2, #4]
.L1235:
	.loc 2 2223 0
	subs	r6, r6, #1
	beq	.L1237
.LVL1046:
	.loc 2 2224 0
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1236
.L1237:
	.loc 2 2234 0
	ldrh	r3, [r4]
	ldr	r1, [r8, #2300]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	.loc 2 2235 0
	ldrh	r3, [r8, #78]
	strh	r3, [r4, #2]	@ movhi
	.loc 2 2236 0
	mov	r3, #0
	strb	r3, [r4, #6]
	.loc 2 2237 0
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2238 0
	sub	sp, fp, #32
.LCFI335:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL1047:
.L1236:
.LCFI336:
	.loc 2 2226 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1048:
	.loc 2 2227 0
	cmn	r0, #1
	.loc 2 2226 0
	str	r0, [fp, #-48]
	.loc 2 2227 0
	beq	.L1237
	.loc 2 2229 0
	ldr	r3, [r8, #2520]
	.loc 2 2230 0
	mov	r1, #1
	sub	r0, fp, #52
	.loc 2 2229 0
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r7
	str	r3, [r8, #2520]
	.loc 2 2230 0
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL1049:
	.loc 2 2231 0
	ldrh	r0, [r4]
	bl	decrement_vpc_count
.LVL1050:
	b	.L1235
.L1246:
	.align	2
.L1245:
	.word	.LANCHOR0
	.word	305419896
.LFE238:
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlLoadBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
.LFB179:
	.loc 4 173 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI337:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI338:
	sub	fp, ip, #4
.LCFI339:
	.loc 4 180 0
	ldr	r4, .L1279
	.loc 4 186 0
	ldr	r7, .L1279+4
	.loc 4 180 0
	ldr	r3, [r4, #3240]
	.loc 4 181 0
	ldr	r6, [r4, #3244]
	.loc 4 180 0
	str	r3, [r4, #3228]
	.loc 4 181 0
	str	r6, [r4, #3232]
	.loc 4 183 0
	bl	FtlBbtMemInit
.LVL1051:
	.loc 4 184 0
	ldrh	r5, [r4, #74]
	sub	r5, r5, #1
	uxth	r5, r5
.LVL1052:
.L1248:
	.loc 4 184 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #74]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L1251
	.loc 4 185 0 is_stmt 1
	lsl	r3, r5, #10
	.loc 4 186 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 185 0
	str	r3, [r4, #3224]
	.loc 4 186 0
	bl	FlashReadPages
.LVL1053:
	.loc 4 187 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	bne	.L1249
	.loc 4 188 0
	ldr	r3, [r4, #3224]
	.loc 4 189 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 188 0
	add	r3, r3, #1
	str	r3, [r4, #3224]
	.loc 4 189 0
	bl	FlashReadPages
.LVL1054:
.L1249:
	.loc 4 191 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	beq	.L1250
	.loc 4 196 0
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1250
	.loc 4 199 0
	ldr	r3, [r6, #4]
	.loc 4 198 0
	strh	r5, [r4, #128]	@ movhi
	.loc 4 199 0
	str	r3, [r4, #136]
	.loc 4 200 0
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #132]	@ movhi
.L1251:
	.loc 4 216 0
	ldrh	r3, [r4, #128]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1265
	.loc 4 222 0
	ldrh	r3, [r4, #132]
	cmp	r3, r2
	beq	.L1255
	.loc 4 224 0
	lsl	r3, r3, #10
	.loc 4 225 0
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1279+4
	.loc 4 224 0
	str	r3, [r4, #3224]
	.loc 4 225 0
	bl	FlashReadPages
.LVL1055:
	.loc 4 226 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	beq	.L1255
	.loc 4 229 0
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1255
	.loc 4 229 0 is_stmt 0 discriminator 1
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #136]
	cmp	r3, r2
	.loc 4 231 0 is_stmt 1 discriminator 1
	ldrhhi	r2, [r4, #132]
	.loc 4 232 0 discriminator 1
	strhi	r3, [r4, #136]
	.loc 4 233 0 discriminator 1
	ldrhhi	r3, [r6, #8]
	.loc 4 231 0 discriminator 1
	strhhi	r2, [r4, #128]	@ movhi
	.loc 4 233 0 discriminator 1
	strhhi	r3, [r4, #132]	@ movhi
.L1255:
	.loc 4 245 0
	ldr	r7, .L1279+4
	.loc 4 238 0
	mov	r1, #1
	ldrh	r0, [r4, #128]
	bl	FtlGetLastWrittenPage
.LVL1056:
	sxth	r5, r0
.LVL1057:
	.loc 4 239 0
	add	r0, r0, #1
	strh	r0, [r4, #130]	@ movhi
.L1257:
	.loc 4 241 0
	cmp	r5, #0
	bge	.L1260
	.loc 4 254 0
	mov	r2, #254
	ldr	r1, .L1279+8
	ldr	r0, .L1279+12
	bl	printk
.LVL1058:
.L1259:
	.loc 4 255 0
	ldrh	r3, [r6, #10]
	.loc 4 257 0
	ldrh	r0, [r6, #12]
	.loc 4 255 0
	strh	r3, [r4, #134]	@ movhi
	.loc 4 257 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1262
	.loc 4 259 0
	ldr	r2, [r4, #4]
	cmp	r0, r2
	beq	.L1262
	.loc 4 259 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	lsr	r3, r3, #2
	.loc 4 261 0 is_stmt 1 discriminator 1
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1262
	.loc 4 263 0
	bl	FtlSysBlkNumInit
.LVL1059:
.L1262:
	ldr	r6, .L1279+16
	.loc 4 173 0 discriminator 1
	mov	r5, #0
.LVL1060:
	.loc 4 269 0 discriminator 1
	ldr	r7, .L1279+20
.L1263:
	.loc 4 268 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	bcc	.L1264
	.loc 4 275 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL1061:
.L1250:
	.loc 4 184 0 discriminator 2
	sub	r5, r5, #1
.LVL1062:
	uxth	r5, r5
.LVL1063:
	b	.L1248
.LVL1064:
.L1260:
	.loc 4 243 0
	ldrh	r3, [r4, #128]
	.loc 4 245 0
	mov	r2, #1
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
.LVL1065:
	.loc 4 247 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	beq	.L1258
	.loc 4 247 0 is_stmt 0 discriminator 1
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1259
.L1258:
	sub	r5, r5, #1
.LVL1066:
	sxth	r5, r5
.LVL1067:
	b	.L1257
.LVL1068:
.L1264:
	.loc 4 269 0 is_stmt 1 discriminator 3
	ldrh	r2, [r7]
	ldr	r1, [r4, #3228]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	.loc 4 268 0 discriminator 3
	add	r5, r5, #1
	.loc 4 269 0 discriminator 3
	bl	ftl_memcpy
.LVL1069:
	b	.L1263
.LVL1070:
.L1265:
	.loc 4 218 0
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1280:
	.align	2
.L1279:
	.word	.LANCHOR0
	.word	.LANCHOR0+3220
	.word	.LANCHOR1+443
	.word	.LC1
	.word	.LANCHOR0+152
	.word	.LANCHOR0+3860
.LFE179:
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
.LFB181:
	.loc 4 317 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI340:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI341:
	sub	fp, ip, #4
.LCFI342:
	sub	sp, sp, #4
	.loc 4 327 0
	mov	r7, #0
.LVL1071:
	ldr	r8, .L1300
	.loc 4 325 0
	bl	FtlBbtMemInit
.LVL1072:
	.loc 4 326 0
	bl	FtlLoadFactoryBbt
.LVL1073:
	.loc 4 327 0
	sub	r5, r8, #156
	sub	r9, r8, #18
	mov	r4, r5
.L1282:
	.loc 4 327 0 is_stmt 0 discriminator 1
	ldrh	r3, [r5, #30]
	cmp	r7, r3
	bcc	.L1288
	mov	r0, #0
.L1289:
	.loc 4 381 0 is_stmt 1 discriminator 1
	ldrh	r2, [r4, #90]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1290
	.loc 4 387 0
	ldrh	r5, [r4, #140]
	.loc 4 396 0
	movw	r6, #65535
	.loc 4 387 0
	sub	r5, r5, #1
	uxth	r5, r5
.L1291:
	.loc 4 387 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #140]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1295
	.loc 4 389 0 is_stmt 1
	mov	r0, r5
	bl	FtlBbmIsBadBlock
.LVL1074:
	cmp	r0, #1
	beq	.L1292
	.loc 4 391 0
	mov	r0, r5
	bl	FlashTestBlk
.LVL1075:
	cmp	r0, #0
	beq	.L1293
	.loc 4 393 0
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.LVL1076:
.L1292:
	.loc 4 387 0 discriminator 2
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1291
.L1288:
	.loc 4 331 0
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	.loc 4 328 0
	ldr	r0, [r5, #3240]
	.loc 4 329 0
	ldr	r10, [r5, #3244]
	.loc 4 331 0
	cmp	r3, r2
	.loc 4 328 0
	str	r0, [r5, #3228]
	.loc 4 329 0
	str	r10, [r5, #3232]
	.loc 4 331 0
	beq	.L1283
	.loc 4 333 0
	ldrh	ip, [r5, #74]
	.loc 4 335 0
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1300+4
	.loc 4 333 0
	mla	ip, r7, ip, r3
.LVL1077:
	.loc 4 334 0
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r5, #3224]
	.loc 4 335 0
	bl	FlashReadPages
.LVL1078:
	.loc 4 336 0
	ldrh	r2, [r5, #74]
	ldr	r1, [r5, #3228]
	ldr	r0, [r8]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
.LVL1079:
	ldr	ip, [fp, #-44]
.LVL1080:
.L1284:
	.loc 4 376 0 discriminator 2
	uxth	r0, ip
	.loc 4 327 0 discriminator 2
	add	r7, r7, #1
	add	r8, r8, #4
	.loc 4 376 0 discriminator 2
	bl	FtlBbmMapBadBlock
.LVL1081:
	b	.L1282
.L1283:
	.loc 4 340 0
	mov	r1, r7
	bl	FlashGetBadBlockList
.LVL1082:
	.loc 4 341 0
	ldr	r1, [r8]
	ldr	r0, [r5, #3228]
	bl	FtlBbt2Bitmap
.LVL1083:
	.loc 4 343 0
	ldrh	r6, [r5, #74]
.L1286:
	.loc 4 347 0
	sub	r6, r6, #1
	uxth	r6, r6
.L1285:
	.loc 4 345 0
	ldrh	r0, [r4, #74]
	smlabb	r0, r0, r7, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
.LVL1084:
	cmp	r0, #1
	beq	.L1286
.LBB488:
.LBB489:
.LBB490:
	.loc 1 202 0
	mov	r1, #16
	ldr	r0, [r4, #3244]
.LBE490:
.LBE489:
.LBE488:
	.loc 4 349 0
	strh	r6, [r9]	@ movhi
.LBB493:
.LBB492:
.LBB491:
	.loc 1 202 0
	bl	__memzero
.LVL1085:
.LBE491:
.LBE492:
.LBE493:
.LBB494:
.LBB495:
.LBB496:
	mov	r1, #4096
	ldr	r0, [r4, #3240]
	bl	__memzero
.LVL1086:
.LBE496:
.LBE495:
.LBE494:
	.loc 4 352 0
	ldr	r3, .L1300+8
	strh	r3, [r10]	@ movhi
	.loc 4 353 0
	mov	r3, #0
	str	r3, [r10, #4]
	.loc 4 354 0
	ldrh	r3, [r9]
	.loc 4 355 0
	ldrh	ip, [r4, #74]
	.loc 4 354 0
	strh	r3, [r10, #2]	@ movhi
	.loc 4 355 0
	ldrh	r3, [r9]
	.loc 4 357 0
	ldr	r1, [r8]
	ldr	r0, [r4, #3228]
	.loc 4 355 0
	mla	ip, r7, ip, r3
.LVL1087:
	.loc 4 356 0
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r4, #3224]
	.loc 4 357 0
	ldr	r3, .L1300+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
.LVL1088:
	.loc 4 358 0
	mov	r2, #1
	ldr	r0, .L1300+4
	mov	r1, r2
	bl	FlashEraseBlocks
.LVL1089:
	.loc 4 359 0
	mov	r3, #1
	ldr	r0, .L1300+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1090:
	.loc 4 360 0
	ldr	r3, [r4, #3220]
	ldr	ip, [fp, #-44]
.LVL1091:
	cmn	r3, #1
	bne	.L1284
	.loc 4 362 0
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
.LVL1092:
	.loc 4 363 0
	b	.L1285
.L1290:
	.loc 4 383 0 discriminator 3
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
.LVL1093:
	mov	r0, r5
	b	.L1289
.L1293:
	.loc 4 396 0
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	.loc 4 398 0
	strheq	r5, [r4, #128]	@ movhi
	beq	.L1292
.L1294:
	.loc 4 402 0
	strh	r5, [r4, #132]	@ movhi
.L1295:
	.loc 4 414 0
	ldrh	r3, [r4, #128]
	.loc 4 411 0
	mov	r5, #0
	.loc 4 414 0
	ldr	r0, [r4, #3760]
	.loc 4 416 0
	mov	r1, #1
	.loc 4 413 0
	str	r5, [r4, #136]
	.loc 4 416 0
	mov	r2, #2
	.loc 4 411 0
	strh	r5, [r4, #130]	@ movhi
	.loc 4 414 0
	lsl	r3, r3, #10
	str	r3, [r0, #4]
	.loc 4 415 0
	ldrh	r3, [r4, #132]
	lsl	r3, r3, #10
	str	r3, [r0, #24]
	.loc 4 416 0
	bl	FlashEraseBlocks
.LVL1094:
	.loc 4 418 0
	ldrh	r0, [r4, #128]
	bl	FtlBbmMapBadBlock
.LVL1095:
	.loc 4 419 0
	ldrh	r0, [r4, #132]
	bl	FtlBbmMapBadBlock
.LVL1096:
	.loc 4 421 0
	bl	FtlBbmTblFlush
.LVL1097:
	.loc 4 422 0
	ldr	r3, [r4, #136]
	.loc 4 425 0
	ldrh	r2, [r4, #132]
	.loc 4 426 0
	strh	r5, [r4, #130]	@ movhi
	.loc 4 422 0
	add	r3, r3, #1
	str	r3, [r4, #136]
	.loc 4 424 0
	ldrh	r3, [r4, #128]
	.loc 4 425 0
	strh	r2, [r4, #128]	@ movhi
	.loc 4 427 0
	strh	r3, [r4, #132]	@ movhi
	.loc 4 429 0
	bl	FtlBbmTblFlush
.LVL1098:
	.loc 4 432 0
	mov	r0, r5
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1301:
	.align	2
.L1300:
	.word	.LANCHOR0+156
	.word	.LANCHOR0+3220
	.word	-3872
	.word	.LANCHOR0+3860
.LFE181:
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
.LFB213:
	.loc 2 835 0
	@ args = 0, pretend = 0, frame = 100
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1099:
	mov	ip, sp
.LCFI343:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI344:
	sub	fp, ip, #4
.LCFI345:
	sub	sp, sp, #100
	.loc 2 835 0
	mov	r7, r0
	.loc 2 843 0
	ldr	r4, .L1311
	.loc 2 835 0
	mov	r5, r1
	str	r2, [fp, #-128]
	.loc 2 843 0
	add	r2, r0, r1
.LVL1100:
	ldrh	r3, [r4, #72]
	cmp	r2, r3
	.loc 2 844 0
	mvnhi	r8, #0
	.loc 2 843 0
	bhi	.L1302
	.loc 2 840 0
	ldrh	r6, [r4, #84]
	mov	r8, #0
	lsr	r6, r0, r6
.LVL1101:
	lsl	r10, r6, #2
.LVL1102:
.L1304:
	.loc 2 846 0
	cmp	r5, #0
	bne	.L1309
.L1302:
	.loc 2 869 0
	mov	r0, r8
	sub	sp, fp, #40
.LCFI346:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1103:
.L1309:
.LCFI347:
	.loc 2 847 0
	ldr	r3, [r4, #3844]
.LVL1104:
	.loc 2 848 0
	mov	r0, r7
	ldrh	r2, [r4, #34]
	.loc 2 847 0
	ldr	ip, [r3, r10]
.LVL1105:
	.loc 2 848 0
	mov	r1, r2
	str	r2, [fp, #-136]
	.loc 2 847 0
	str	ip, [fp, #-140]
	.loc 2 848 0
	bl	__umodsi3
.LVL1106:
	.loc 2 849 0
	ldr	r2, [fp, #-136]
	.loc 2 850 0
	ldr	ip, [fp, #-140]
.LVL1107:
	.loc 2 848 0
	str	r0, [fp, #-132]
	.loc 2 849 0
	sub	r3, r2, r0
	uxth	r9, r3
	.loc 2 850 0
	cmp	r5, r9
	.loc 2 851 0
	uxthcc	r9, r5
	.loc 2 852 0
	cmp	ip, #0
	cmpne	r9, r2
	movne	r1, #1
	moveq	r1, #0
	beq	.L1306
	.loc 2 854 0
	ldr	r2, [r4, #3808]
	.loc 2 856 0
	sub	r0, fp, #124
	.loc 2 853 0
	str	ip, [fp, #-120]
	.loc 2 854 0
	str	r2, [fp, #-116]
	.loc 2 855 0
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	.loc 2 856 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL1108:
.L1307:
	.loc 2 860 0
	ldr	r3, [fp, #-132]
	lsl	ip, r9, #9
	ldr	r0, [r4, #3808]
	.loc 2 864 0
	sub	r5, r5, r9
	.loc 2 860 0
	mov	r2, ip
	ldr	r1, [fp, #-128]
	str	ip, [fp, #-136]
	.loc 2 865 0
	add	r7, r7, r9
	add	r10, r10, #4
	.loc 2 860 0
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
.LVL1109:
	.loc 2 861 0
	mov	r1, r6
	ldr	r2, [r4, #3808]
	ldr	r0, .L1311+4
	.loc 2 863 0
	add	r6, r6, #1
	.loc 2 861 0
	bl	FtlMapWritePage
.LVL1110:
	.loc 2 866 0
	ldr	r3, [fp, #-128]
.LVL1111:
	.loc 2 862 0
	cmn	r0, #1
	.loc 2 866 0
	ldr	ip, [fp, #-136]
	.loc 2 862 0
	mvneq	r8, #0
	.loc 2 866 0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1304
.LVL1112:
.L1306:
	.loc 2 858 0
	ldrh	r2, [r4, #86]
	ldr	r0, [r4, #3808]
	bl	ftl_memset
.LVL1113:
	b	.L1307
.L1312:
	.align	2
.L1311:
	.word	.LANCHOR0
	.word	.LANCHOR0+3908
.LFE213:
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
.LFB236:
	.loc 2 2159 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI348:
	push	{fp, ip, lr, pc}
.LCFI349:
	sub	fp, ip, #4
.LCFI350:
	.loc 2 2160 0
	ldr	r3, .L1315
	ldr	r2, .L1315+4
	ldr	r1, [r3, #2612]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
.LBB499:
.LBB500:
	.loc 2 2161 0
	ldr	r2, .L1315+8
	.loc 2 2174 0
	mov	r1, #1
	mov	r0, #0
	.loc 2 2161 0
	str	r2, [r3, #2616]
	.loc 2 2162 0
	ldr	r2, [r3, #2508]
	str	r2, [r3, #2700]
	.loc 2 2163 0
	ldr	r2, [r3, #2512]
	str	r2, [r3, #2704]
	.loc 2 2164 0
	ldr	r2, [r3, #2504]
	str	r2, [r3, #2620]
	.loc 2 2165 0
	ldr	r2, [r3, #2492]
	str	r2, [r3, #2624]
	.loc 2 2166 0
	ldr	r2, [r3, #2484]
	str	r2, [r3, #2628]
	.loc 2 2167 0
	ldr	r2, [r3, #2500]
	str	r2, [r3, #2632]
	.loc 2 2168 0
	ldr	r2, [r3, #2528]
	str	r2, [r3, #2640]
	.loc 2 2169 0
	ldr	r2, [r3, #2536]
	str	r2, [r3, #2644]
	.loc 2 2170 0
	ldr	r2, [r3, #2488]
	str	r2, [r3, #2648]
	.loc 2 2171 0
	ldr	r2, [r3, #2496]
	str	r2, [r3, #2652]
	.loc 2 2172 0
	ldr	r2, [r3, #2540]
	str	r2, [r3, #2656]
	.loc 2 2173 0
	ldr	r2, [r3, #2544]
	str	r2, [r3, #2660]
	.loc 2 2174 0
	ldr	r2, .L1315+12
	bl	FtlVendorPartWrite
.LVL1114:
	ldmfd	sp, {fp, sp, pc}
.L1316:
	.align	2
.L1315:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177348
	.word	.LANCHOR0+2612
.LBE500:
.LBE499:
.LFE236:
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
.LFB215:
	.loc 2 910 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1115:
	mov	ip, sp
.LCFI351:
	push	{fp, ip, lr, pc}
.LCFI352:
	sub	fp, ip, #4
.LCFI353:
	.loc 2 913 0
	ldr	r2, .L1322
	ldrh	r3, [r2]
	cmp	r3, #31
	.loc 2 914 0
	addls	r3, r3, #1
	.loc 2 911 0
	movhi	r2, #32
.LVL1116:
	.loc 2 914 0
	strhls	r3, [r2]	@ movhi
.LVL1117:
	.loc 2 915 0
	movls	r2, #1
.LVL1118:
	.loc 2 917 0
	cmp	r0, #0
	ldr	r3, .L1322+4
	bne	.L1319
	.loc 2 917 0 is_stmt 0 discriminator 1
	ldr	r1, [r3, #3820]
	ldr	r0, [r1, #20]
.LVL1119:
	ldr	r1, [r1, #16]
	add	r2, r2, r0
.LVL1120:
	cmp	r1, r2
	bcc	.L1320
.LVL1121:
.L1319:
	.loc 2 919 0 is_stmt 1
	ldr	r2, [r3, #3820]
.LVL1122:
	.loc 2 924 0
	mov	r0, #64
.LVL1123:
	.loc 2 919 0
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	.loc 2 920 0
	ldr	r1, .L1322+8
	str	r1, [r2]
	.loc 2 921 0
	movw	r1, #3812
	ldrh	r1, [r3, r1]
	ldr	r2, [r3, #3820]
	lsl	r3, r1, #9
	str	r3, [r2, #12]
	.loc 2 922 0
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	.loc 2 923 0
	mov	r3, #0
	str	r3, [r2, #4]
	.loc 2 924 0
	bl	FtlVendorPartWrite
.LVL1124:
	.loc 2 925 0
	bl	Ftl_save_ext_data
.LVL1125:
.L1320:
	.loc 2 927 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1323:
	.align	2
.L1322:
	.word	.LANCHOR3-4080
	.word	.LANCHOR0
	.word	1112818501
.LFE215:
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
.LFB214:
	.loc 2 872 0
	@ args = 0, pretend = 0, frame = 92
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1126:
	mov	ip, sp
.LCFI354:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI355:
	sub	fp, ip, #4
.LCFI356:
	sub	sp, sp, #92
	.loc 2 872 0
	mov	r9, r2
	.loc 2 881 0
	ldr	r5, .L1333
	.loc 2 872 0
	mov	r7, r1
	.loc 2 881 0
	add	r1, r0, r1
.LVL1127:
	.loc 2 872 0
	mov	r10, r0
	.loc 2 881 0
	ldrh	r2, [r5, #72]
.LVL1128:
	cmp	r1, r2
	.loc 2 882 0
	mvnhi	r8, #0
	.loc 2 881 0
	bhi	.L1324
	.loc 2 877 0
	ldrh	r6, [r5, #84]
	mov	r8, #0
	lsr	r6, r0, r6
.LVL1129:
	lsl	r6, r6, #2
.LVL1130:
.L1326:
	.loc 2 883 0
	cmp	r7, #0
	bne	.L1331
.L1324:
	.loc 2 906 0
	mov	r0, r8
	sub	sp, fp, #40
.LCFI357:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1131:
.L1331:
.LCFI358:
	.loc 2 884 0
	ldr	r2, [r5, #3844]
	.loc 2 885 0
	mov	r0, r10
	ldrh	r4, [r5, #34]
.LVL1132:
	.loc 2 884 0
	ldr	ip, [r2, r6]
.LVL1133:
	.loc 2 885 0
	mov	r1, r4
	.loc 2 884 0
	str	ip, [fp, #-128]
	.loc 2 885 0
	bl	__umodsi3
.LVL1134:
	.loc 2 886 0
	sub	r4, r4, r0
	.loc 2 887 0
	ldr	ip, [fp, #-128]
.LVL1135:
	.loc 2 886 0
	uxth	r4, r4
.LVL1136:
	str	r0, [fp, #-132]
	.loc 2 887 0
	cmp	r7, r4
	.loc 2 888 0
	uxthcc	r4, r7
	.loc 2 889 0
	cmp	ip, #0
	lsl	r3, r4, #9
	str	r3, [fp, #-128]
	beq	.L1328
	.loc 2 891 0
	ldr	r2, [r5, #3808]
	.loc 2 892 0
	sub	r3, fp, #104
	.loc 2 893 0
	sub	r0, fp, #124
	.loc 2 892 0
	str	r3, [fp, #-112]
	.loc 2 890 0
	str	ip, [fp, #-120]
	.loc 2 891 0
	str	r2, [fp, #-116]
	.loc 2 893 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL1137:
	.loc 2 896 0
	ldr	r1, [r5, #3808]
	mov	r0, r9
	ldr	r3, [fp, #-132]
	.loc 2 894 0
	ldr	r2, [fp, #-124]
	.loc 2 896 0
	add	r1, r1, r3, lsl #9
	.loc 2 895 0
	cmn	r2, #1
	.loc 2 896 0
	lsl	r2, r4, #9
	.loc 2 895 0
	mvneq	r8, #0
	.loc 2 896 0
	bl	ftl_memcpy
.LVL1138:
.L1330:
	.loc 2 903 0
	ldr	r3, [fp, #-128]
	.loc 2 901 0
	sub	r7, r7, r4
	.loc 2 902 0
	add	r10, r10, r4
	add	r6, r6, #4
	.loc 2 903 0
	add	r9, r9, r3
	b	.L1326
.LVL1139:
.L1328:
	.loc 2 898 0
	lsl	r2, r4, #9
	mov	r1, ip
	mov	r0, r9
	bl	ftl_memset
.LVL1140:
	b	.L1330
.L1334:
	.align	2
.L1333:
	.word	.LANCHOR0
.LFE214:
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
.LFB221:
	.loc 2 1383 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI359:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI360:
	sub	fp, ip, #4
.LCFI361:
	.loc 2 1384 0
	ldr	r4, .L1337
	movw	r5, #3812
	mov	r0, #64
	ldr	r2, [r4, #3820]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
.LVL1141:
	.loc 2 1385 0
	ldr	r3, [r4, #3820]
	ldr	r2, [r3]
	ldr	r3, .L1337+4
	cmp	r2, r3
	beq	.L1336
.LBB503:
.LBB504:
	.loc 2 1387 0
	ldr	r1, .L1337+8
	ldr	r0, .L1337+12
	bl	printk
.LVL1142:
	.loc 2 1388 0
	ldrh	r2, [r4, r5]
	mov	r1, #0
	ldr	r0, [r4, #3820]
	lsl	r2, r2, #9
	bl	ftl_memset
.LVL1143:
.L1336:
.LBE504:
.LBE503:
	.loc 2 1393 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1338:
	.align	2
.L1337:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC99
	.word	.LC71
.LFE221:
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
.LFB237:
	.loc 2 2179 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI362:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI363:
	sub	fp, ip, #4
.LCFI364:
	.loc 2 2180 0
	ldr	r4, .L1342
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1342+4
	bl	FtlVendorPartRead
.LVL1144:
	.loc 2 2181 0
	ldr	r5, .L1342+8
	ldr	r3, [r4, #2612]
	cmp	r3, r5
	beq	.L1340
.LBB510:
.LBB511:
.LBB512:
	.loc 1 202 0
	mov	r1, #512
	ldr	r0, .L1342+4
	bl	__memzero
.LVL1145:
.LBE512:
.LBE511:
.LBE510:
	.loc 2 2183 0
	str	r5, [r4, #2612]
.L1340:
	.loc 2 2186 0
	ldr	r3, [r4, #2612]
	cmp	r3, r5
	bne	.L1341
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
.L1341:
	.loc 2 2201 0
	ldrh	r2, [r4, #68]
	ldr	r3, [r4, #2528]
	ldr	r0, [r4, #2524]
	ldrh	r1, [r4, #16]
	mla	r0, r0, r2, r3
	bl	__udivsi3
.LVL1146:
	str	r0, [r4, #2532]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1343:
	.align	2
.L1342:
	.word	.LANCHOR0
	.word	.LANCHOR0+2612
	.word	1179929683
.LFE237:
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlVpcTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
.LFB219:
	.loc 2 1053 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI365:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI366:
	sub	fp, ip, #4
.LCFI367:
	.loc 2 1059 0
	ldr	r4, .L1356
	.loc 2 1065 0
	mov	r6, #0
	.loc 2 1096 0
	mov	r1, #255
	.loc 2 1062 0
	ldr	r5, .L1356+4
	.loc 2 1059 0
	ldr	r3, [r4, #3240]
	.loc 2 1060 0
	ldr	r7, [r4, #3244]
	.loc 2 1070 0
	sub	r8, r5, #308
	.loc 2 1124 0
	ldr	r10, .L1356+8
	.loc 2 1059 0
	str	r3, [r4, #3228]
	.loc 2 1062 0
	movw	r3, #2552
	ldrh	r3, [r4, r3]
	.loc 2 1105 0
	add	r9, r5, #668
	.loc 2 1060 0
	str	r7, [r4, #3232]
	.loc 2 1066 0
	str	r6, [r7, #12]
	.loc 2 1062 0
	strh	r3, [r7, #2]	@ movhi
	.loc 2 1063 0
	ldr	r3, .L1356+12
	strh	r3, [r7]	@ movhi
	.loc 2 1064 0
	ldr	r3, [r4, #2560]
	.loc 2 1065 0
	str	r6, [r7, #8]
	.loc 2 1074 0
	ldrh	r2, [r5, #-226]
	.loc 2 1064 0
	str	r3, [r7, #4]
	.loc 2 1068 0
	ldr	r3, .L1356+16
	str	r3, [r4, #2244]
	.loc 2 1069 0
	ldr	r3, .L1356+20
	str	r3, [r4, #2248]
	.loc 2 1070 0
	ldrh	r3, [r5, #6]
	strh	r3, [r8, #8]	@ movhi
	.loc 2 1071 0
	ldrh	r3, [r4, #30]
	strb	r3, [r4, #2254]
	.loc 2 1073 0
	movw	r3, #2324
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #14]	@ movhi
	.loc 2 1074 0
	ldrb	r3, [r4, #2330]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	.loc 2 1078 0
	ldrh	r2, [r5, #-178]
	.loc 2 1074 0
	strh	r3, [r8, #16]	@ movhi
	.loc 2 1075 0
	ldrb	r3, [r4, #2332]	@ zero_extendqisi2
	strb	r3, [r4, #2255]
	.loc 2 1077 0
	movw	r3, #2372
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #18]	@ movhi
	.loc 2 1078 0
	ldrb	r3, [r4, #2378]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r8, #20]	@ movhi
	.loc 2 1079 0
	ldrb	r3, [r4, #2380]	@ zero_extendqisi2
	strb	r3, [r4, #2256]
	.loc 2 1081 0
	movw	r3, #2420
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #22]	@ movhi
	.loc 2 1082 0
	ldrh	r2, [r5, #-130]
	ldrb	r3, [r4, #2426]	@ zero_extendqisi2
	.loc 2 1096 0
	ldr	r0, [r4, #3228]
	.loc 2 1082 0
	orr	r3, r3, r2, lsl #6
	.loc 2 1096 0
	ldrh	r2, [r4, #86]
	.loc 2 1082 0
	strh	r3, [r8, #24]	@ movhi
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
.LVL1147:
	.loc 2 1097 0
	mov	r1, r8
	mov	r2, #48
	ldr	r0, [r4, #3228]
	.loc 2 1054 0
	movw	r8, #65535
.LVL1148:
	.loc 2 1097 0
	bl	ftl_memcpy
.LVL1149:
	.loc 2 1098 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #3228]
	ldr	r1, [r4, #2300]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
.LVL1150:
	.loc 2 1099 0
	ldrh	r0, [r4, #16]
	ldr	r3, [r4, #3228]
	ldr	r1, [r4]
	lsr	r2, r0, #3
	add	r0, r0, #24
	lsl	r0, r0, #1
	add	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r3, r0
	bl	ftl_memcpy
.LVL1151:
	.loc 2 1100 0
	mov	r0, r6
	bl	FtlUpdateVaildLpn
.LVL1152:
.L1345:
	.loc 2 1102 0
	ldr	r3, [r4, #3240]
	.loc 2 1105 0
	mov	r0, r9
	.loc 2 1104 0
	ldrh	r2, [r5]
	.loc 2 1102 0
	str	r3, [r4, #3228]
	.loc 2 1103 0
	ldr	r3, [r4, #3244]
	str	r3, [r4, #3232]
	.loc 2 1104 0
	ldrh	r3, [r5, #2]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3224]
	.loc 2 1105 0
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1153:
	.loc 2 1106 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r5, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1346
	.loc 2 1108 0
	ldrh	r3, [r5]
	.loc 2 1107 0
	ldrh	r8, [r5, #4]
	.loc 2 1108 0
	strh	r3, [r5, #4]	@ movhi
	.loc 2 1109 0
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	.loc 2 1110 0
	bl	FtlFreeSysBlkQueueOut
.LVL1154:
	.loc 2 1115 0
	ldr	r3, [r4, #2516]
	.loc 2 1110 0
	strh	r0, [r5]	@ movhi
	.loc 2 1115 0
	add	r2, r3, #1
	str	r3, [r4, #2560]
	str	r2, [r4, #2516]
	.loc 2 1116 0
	lsl	r2, r0, #10
	str	r2, [r4, #3224]
	.loc 2 1117 0
	str	r3, [r7, #4]
	.loc 2 1119 0
	mov	r3, #1
	.loc 2 1118 0
	strh	r0, [r7, #2]	@ movhi
	.loc 2 1119 0
	mov	r2, r3
	mov	r1, r3
	mov	r0, r9
	bl	FlashProgPages
.LVL1155:
.L1346:
	.loc 2 1121 0
	ldrh	r3, [r5, #2]
	.loc 2 1122 0
	ldr	r2, [r4, #3220]
	.loc 2 1121 0
	add	r3, r3, #1
	uxth	r3, r3
	.loc 2 1122 0
	cmn	r2, #1
	.loc 2 1121 0
	strh	r3, [r5, #2]	@ movhi
	.loc 2 1122 0
	bne	.L1347
	.loc 2 1124 0
	cmp	r3, #1
	bne	.L1348
	.loc 2 1124 0 is_stmt 0 discriminator 1
	movw	r2, #1124
	mov	r1, r10
	ldr	r0, .L1356+24
	bl	printk
.LVL1156:
.L1348:
	.loc 2 1125 0 is_stmt 1
	ldrh	r3, [r5, #2]
	.loc 2 1127 0
	add	r6, r6, #1
.LVL1157:
	uxth	r6, r6
.LVL1158:
	.loc 2 1125 0
	cmp	r3, #1
	.loc 2 1126 0
	ldrheq	r3, [r4, #80]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	.loc 2 1128 0
	cmp	r6, #3
	bls	.L1345
	.loc 2 1129 0
	mov	r2, r6
	ldr	r1, [r4, #3224]
	ldr	r0, .L1356+28
	bl	printk
.LVL1159:
.L1351:
	b	.L1351
.L1347:
	.loc 2 1135 0
	cmp	r3, #1
	beq	.L1345
	.loc 2 1138 0
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1352
	.loc 2 1139 0
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.LVL1160:
.L1352:
	.loc 2 1141 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1357:
	.align	2
.L1356:
	.word	.LANCHOR0
	.word	.LANCHOR0+2552
	.word	.LANCHOR1+454
	.word	-3932
	.word	1179929683
	.word	1342177348
	.word	.LC1
	.word	.LC100
.LFE219:
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
.LFB239:
	.loc 2 2242 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI368:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI369:
	sub	fp, ip, #4
.LCFI370:
	sub	sp, sp, #4
	.loc 2 2249 0
	mov	r5, #0
.LVL1161:
	.loc 2 2248 0
	ldr	r4, .L1370
	.loc 2 2247 0
	ldr	r1, .L1370+4
	ldr	r0, .L1370+8
	bl	printk
.LVL1162:
	.loc 2 2248 0
	ldrh	r2, [r4, #18]
	mov	r1, #0
	ldr	r0, [r4, #3824]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL1163:
.L1359:
	.loc 2 2249 0 discriminator 1
	ldr	r3, [r4, #2480]
	cmp	r5, r3
	bcc	.L1361
	.loc 2 2259 0 discriminator 1
	ldr	r9, .L1370+12
	mov	r6, #0
	.loc 2 2260 0 discriminator 1
	ldr	r10, .L1370+16
.LVL1164:
.L1362:
	.loc 2 2256 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r5, r6
.LVL1165:
	cmp	r3, r5
	bhi	.L1366
	.loc 2 2268 0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1166:
.L1361:
	.loc 2 2250 0
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
.LVL1167:
	.loc 2 2251 0
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1360
	.loc 2 2252 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1168:
	.loc 2 2253 0
	ldr	r2, [r4, #3824]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1360:
	.loc 2 2249 0 discriminator 2
	add	r5, r5, #1
	b	.L1359
.LVL1169:
.L1366:
	.loc 2 2257 0
	uxth	r8, r6
	ldr	r3, [r4, #2300]
	movw	r1, #65535
	lsl	r7, r8, #1
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #3824]
	ldrh	r3, [r3, r7]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L1364
	.loc 2 2259 0
	mov	r1, r8
	mov	r0, r9
	bl	printk
.LVL1170:
	.loc 2 2260 0
	ldrh	r3, [r10]
	cmp	r3, r5
	beq	.L1364
	.loc 2 2260 0 is_stmt 0 discriminator 1
	ldr	r3, .L1370+20
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L1364
	.loc 2 2260 0 discriminator 2
	ldrh	r3, [r3, #-48]
	cmp	r3, r5
	beq	.L1364
	.loc 2 2261 0 is_stmt 1
	ldr	r3, [r4, #3824]
	.loc 2 2262 0
	mov	r0, r8
	.loc 2 2261 0
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #2300]
	strh	r2, [r3, r7]	@ movhi
	.loc 2 2262 0
	bl	update_vpc_list
.LVL1171:
	.loc 2 2263 0
	bl	l2p_flush
.LVL1172:
	.loc 2 2264 0
	bl	FtlVpcTblFlush
.LVL1173:
.L1364:
	add	r6, r6, #1
	b	.L1362
.L1371:
	.align	2
.L1370:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC90
	.word	.LC101
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2420
.LFE239:
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
.LFB249:
	.loc 2 2679 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1174:
	mov	ip, sp
.LCFI371:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
.LCFI372:
	sub	fp, ip, #4
.LCFI373:
	.loc 2 2681 0
	ldr	r5, .L1382
	.loc 2 2679 0
	mov	r7, r0
	.loc 2 2680 0
	ldrh	r4, [r0]
.LVL1175:
	.loc 2 2681 0
	ldrh	r3, [r5, #16]
	cmp	r3, r4
	bcs	.L1373
	.loc 2 2681 0 is_stmt 0 discriminator 1
	movw	r2, #2681
	ldr	r1, .L1382+4
	ldr	r0, .L1382+8
	bl	printk
.LVL1176:
.L1373:
	.loc 2 2683 0 is_stmt 1
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1374
	.loc 2 2684 0
	ldr	r2, [r5, #2300]
	lsl	r3, r4, #1
	.loc 2 2685 0
	mov	r0, r4
	.loc 2 2684 0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1375
	.loc 2 2685 0
	bl	INSERT_DATA_LIST
.LVL1177:
.L1374:
	.loc 2 2689 0
	mov	r3, #1
	.loc 2 2690 0
	movw	r2, #65535
	.loc 2 2689 0
	strb	r3, [r7, #8]
	.loc 2 2690 0
	ldr	r3, .L1382+12
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r2
	beq	.L1376
	.loc 2 2691 0
	cmp	r4, r0
	bne	.L1377
	.loc 2 2691 0 is_stmt 0 discriminator 1
	ldr	r2, [r5, #2300]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1378
.L1377:
	.loc 2 2692 0 is_stmt 1
	bl	update_vpc_list
.LVL1178:
.L1378:
	.loc 2 2693 0
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L1376:
	.loc 2 2695 0
	mov	r0, r7
	bl	allocate_data_superblock
.LVL1179:
	.loc 2 2696 0
	bl	l2p_flush
.LVL1180:
	.loc 2 2697 0
	mov	r0, #0
	bl	FtlEctTblFlush
.LVL1181:
	.loc 2 2698 0
	bl	FtlVpcTblFlush
.LVL1182:
	.loc 2 2700 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1375:
	.loc 2 2687 0
	bl	INSERT_FREE_LIST
.LVL1183:
	b	.L1374
.L1383:
	.align	2
.L1382:
	.word	.LANCHOR0
	.word	.LANCHOR1+490
	.word	.LC1
	.word	.LANCHOR0+3952
.LFE249:
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
.LFB165:
	.loc 3 904 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1184:
	mov	ip, sp
.LCFI374:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI375:
	sub	fp, ip, #4
.LCFI376:
	sub	sp, sp, #8
	.loc 3 913 0
	mov	r7, #0
.LVL1185:
	ldr	r6, .L1398
	.loc 3 904 0
	mov	r4, r3
	.loc 3 933 0
	ldr	r10, .L1398+4
	.loc 3 911 0
	mov	r2, #0
.LVL1186:
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
.LVL1187:
	.loc 3 904 0
	mov	r5, r0
	mov	r9, r1
	.loc 3 911 0
	bl	FlashProgPages
.LVL1188:
.L1385:
	.loc 3 913 0 discriminator 1
	cmp	r7, r9
	beq	.L1392
	.loc 3 919 0
	mov	r8, #0
	b	.L1393
.LVL1189:
.L1388:
	.loc 3 915 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1190:
	.loc 3 916 0
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1386
	.loc 3 917 0
	ldr	r1, [r6, #2300]
	lsl	r3, r3, #1
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	.loc 3 918 0
	ldrh	r3, [r6, #78]
	.loc 3 919 0
	strb	r8, [r4, #6]
	.loc 3 920 0
	strh	r8, [r4, #4]	@ movhi
	.loc 3 918 0
	strh	r3, [r4, #2]	@ movhi
.L1386:
	.loc 3 922 0
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1387
	.loc 3 923 0
	mov	r0, r4
	bl	allocate_new_data_superblock
.LVL1191:
.L1387:
	.loc 3 926 0
	ldr	r3, [r6, #2708]
	add	r3, r3, #1
	str	r3, [r6, #2708]
	.loc 3 927 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
.LVL1192:
	.loc 3 928 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1193:
	.loc 3 930 0
	mov	r2, #0
	.loc 3 929 0
	str	r0, [r5, #4]
	.loc 3 928 0
	str	r0, [fp, #-44]
	.loc 3 930 0
	mov	r1, #1
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r0, r5
	bl	FlashProgPages
.LVL1194:
.L1393:
	.loc 3 914 0
	ldr	r3, [r5]
.LVL1195:
	cmn	r3, #1
	beq	.L1388
	.loc 3 933 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
	cmp	r2, r3
	bcc	.L1389
	.loc 3 933 0 is_stmt 0 discriminator 1
	movw	r2, #933
	mov	r1, r10
	ldr	r0, .L1398+8
	bl	printk
.LVL1196:
.L1389:
	.loc 3 934 0 is_stmt 1
	ldr	r3, [r5, #4]
	sub	r1, fp, #40
	.loc 3 935 0
	mov	r2, #1
	ldr	r0, [r5, #16]
	.loc 3 934 0
	str	r3, [r1, #-4]!
.LVL1197:
	.loc 3 935 0
	bl	log2phys
.LVL1198:
	.loc 3 937 0
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #12]
.LVL1199:
	.loc 3 939 0
	ubfx	r0, r3, #10, #16
	str	r3, [fp, #-48]
	bl	P2V_block_in_plane
.LVL1200:
	.loc 3 940 0
	ldr	r3, [fp, #-48]
.LVL1201:
	.loc 3 939 0
	mov	r8, r0
	.loc 3 940 0
	cmn	r3, #1
	beq	.L1390
	.loc 3 942 0
	ldr	r2, [r6, #2300]
	lsl	r3, r0, #1
.LVL1202:
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1391
	.loc 3 943 0
	mov	r1, r0
	ldr	r0, .L1398+12
	bl	printk
.LVL1203:
.L1391:
	.loc 3 944 0
	mov	r0, r8
	bl	decrement_vpc_count
.LVL1204:
.L1390:
	.loc 3 913 0 discriminator 2
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L1385
.LVL1205:
.L1392:
	.loc 3 948 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
.LVL1206:
	cmp	r2, r3
	bcc	.L1384
	.loc 3 948 0 is_stmt 0 discriminator 1
	mov	r2, #948
	ldr	r1, .L1398+4
	ldr	r0, .L1398+8
	bl	printk
.LVL1207:
.L1384:
	.loc 3 949 0 is_stmt 1
	sub	sp, fp, #40
.LCFI377:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1208:
.L1399:
	.align	2
.L1398:
	.word	.LANCHOR0
	.word	.LANCHOR1+519
	.word	.LC1
	.word	.LC102
.LFE165:
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlCacheWriteBack
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
.LFB166:
	.loc 3 952 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI378:
	push	{r4, fp, ip, lr, pc}
.LCFI379:
	sub	fp, ip, #4
.LCFI380:
	.loc 3 953 0
	ldr	r4, .L1405
	ldr	r0, [r4, #3948]
	cmp	r0, #0
	beq	.L1401
	.loc 3 954 0
	ldr	r3, .L1405+4
	mov	r2, #0
	mov	r1, #1
	ldr	r3, [r3, #-4076]
	bl	FtlProgPages
.LVL1209:
	.loc 3 955 0
	mov	r3, #0
	str	r3, [r4, #3948]
.L1401:
	.loc 3 958 0
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L1406:
	.align	2
.L1405:
	.word	.LANCHOR0
	.word	.LANCHOR3
.LFE166:
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
.LFB162:
	.loc 3 797 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI381:
	push	{fp, ip, lr, pc}
.LCFI382:
	sub	fp, ip, #4
.LCFI383:
	.loc 3 798 0
	bl	FtlCacheWriteBack
.LVL1210:
	.loc 3 799 0
	bl	l2p_flush
.LVL1211:
	.loc 3 800 0
	mov	r0, #1
	bl	FtlEctTblFlush
.LVL1212:
	.loc 3 801 0
	bl	FtlVpcTblFlush
.LVL1213:
	.loc 3 803 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.LFE162:
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
.LFB163:
	.loc 3 806 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI384:
	push	{fp, ip, lr, pc}
.LCFI385:
	sub	fp, ip, #4
.LCFI386:
	.loc 3 807 0
	ldr	r3, .L1410
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1409
	.loc 3 808 0
	bl	FtlSysFlush
.LVL1214:
.L1409:
	.loc 3 810 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1411:
	.align	2
.L1410:
	.word	.LANCHOR2
.LFE163:
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	FtlDiscard
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDiscard, %function
FtlDiscard:
.LFB168:
	.loc 3 1157 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1215:
	mov	ip, sp
.LCFI387:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI388:
	sub	fp, ip, #4
.LCFI389:
	sub	sp, sp, #8
	.loc 3 1164 0
	add	r2, r0, r1
	ldr	r6, .L1427
	.loc 3 1157 0
	mov	r7, r0
	mov	r5, r1
	.loc 3 1164 0
	ldr	r3, [r6, #116]
	cmp	r2, r3
	.loc 3 1165 0
	mvnhi	r0, #0
.LVL1216:
	.loc 3 1164 0
	bhi	.L1412
	.loc 3 1167 0
	cmp	r1, #31
	bhi	.L1414
.LVL1217:
.L1419:
	.loc 3 1168 0
	mov	r0, #0
.L1412:
	.loc 3 1201 0
	sub	sp, fp, #32
.LCFI390:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL1218:
.L1414:
.LCFI391:
	.loc 3 1170 0
	bl	FtlCacheWriteBack
.LVL1219:
	.loc 3 1172 0
	ldrh	r4, [r6, #34]
	mov	r0, r7
	mov	r1, r4
	bl	__udivsi3
.LVL1220:
	.loc 3 1173 0
	smulbb	r3, r0, r4
	.loc 3 1172 0
	mov	r8, r0
.LVL1221:
	.loc 3 1173 0
	sub	r7, r7, r3
.LVL1222:
	uxth	r7, r7
.LVL1223:
	.loc 3 1174 0
	cmp	r7, #0
	beq	.L1415
	.loc 3 1175 0
	sub	r4, r4, r7
	.loc 3 1176 0
	add	r8, r0, #1
	.loc 3 1175 0
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	.loc 3 1177 0
	sub	r5, r5, r4
.L1415:
	.loc 3 1184 0
	ldr	r4, .L1427+4
	.loc 3 1180 0
	mvn	r3, #0
	str	r3, [fp, #-36]
.LVL1224:
.L1416:
	.loc 3 1181 0
	ldrh	r3, [r6, #34]
	cmp	r5, r3
	bcs	.L1418
	.loc 3 1194 0
	ldr	r3, .L1427+4
	ldr	r2, [r3, #-4072]
	cmp	r2, #32
	bls	.L1419
	.loc 3 1196 0
	mov	r4, #0
	str	r4, [r3, #-4072]
	.loc 3 1197 0
	bl	l2p_flush
.LVL1225:
	.loc 3 1198 0
	bl	FtlVpcTblFlush
.LVL1226:
	b	.L1419
.L1418:
	.loc 3 1182 0
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r8
	bl	log2phys
.LVL1227:
	.loc 3 1183 0
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1417
	.loc 3 1184 0
	ldr	r3, [r4, #-4072]
	.loc 3 1186 0
	mov	r2, #1
	sub	r1, fp, #36
	mov	r0, r8
	.loc 3 1184 0
	add	r3, r3, #1
	str	r3, [r4, #-4072]
	.loc 3 1185 0
	ldr	r3, [r6, #2488]
	add	r3, r3, #1
	str	r3, [r6, #2488]
	.loc 3 1186 0
	bl	log2phys
.LVL1228:
	.loc 3 1187 0
	ldr	r0, [fp, #-40]
.LVL1229:
	ubfx	r0, r0, #10, #16
.LVL1230:
	bl	P2V_block_in_plane
.LVL1231:
	.loc 3 1188 0
	bl	decrement_vpc_count
.LVL1232:
.L1417:
	.loc 3 1191 0
	ldrh	r3, [r6, #34]
	.loc 3 1190 0
	add	r8, r8, #1
	.loc 3 1191 0
	sub	r5, r5, r3
	b	.L1416
.L1428:
	.align	2
.L1427:
	.word	.LANCHOR0
	.word	.LANCHOR3
.LFE168:
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
.LFB258:
	.loc 5 112 0
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1233:
	mov	ip, sp
.LCFI392:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI393:
	sub	fp, ip, #4
.LCFI394:
	sub	sp, sp, #4
	.loc 5 119 0
	movw	r3, #2420
	.loc 5 115 0
	ldr	r4, .L1455
	.loc 5 119 0
	movw	r2, #65535
	ldrh	r5, [r4, r3]
	.loc 5 115 0
	ldrh	r1, [r4, #78]
.LVL1234:
	.loc 5 119 0
	cmp	r5, r2
	bne	.L1430
.LVL1235:
.L1440:
	.loc 5 152 0
	mov	r3, #0
	str	r3, [r4, #3960]
	.loc 5 153 0
	movw	r3, #2420
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1453
.L1431:
	.loc 5 154 0
	mov	r0, #0
.LVL1236:
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1237:
.L1430:
	.loc 5 121 0
	cmp	r0, #0
	beq	.L1433
	.loc 5 123 0
	ldr	r3, .L1455+4
	ldrh	r0, [r3, #4]
.LVL1238:
	cmp	r0, r2
	beq	.L1434
.L1435:
	.loc 5 122 0
	mov	r1, #2
.LVL1239:
.L1433:
	.loc 5 131 0
	ldr	r0, .L1455+8
.LVL1240:
	bl	FtlGcScanTempBlk
.LVL1241:
	.loc 5 132 0
	cmn	r0, #1
	.loc 5 131 0
	str	r0, [fp, #-44]
.LVL1242:
	.loc 5 132 0
	beq	.L1436
	.loc 5 135 0
	ldr	r2, [r4, #2304]
	lsl	r5, r5, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L1437
	.loc 5 136 0
	sub	r3, r3, #5
	.loc 5 137 0
	mov	r0, #1
	.loc 5 136 0
	strh	r3, [r2, r5]	@ movhi
	.loc 5 137 0
	bl	FtlEctTblFlush
.LVL1243:
.L1437:
	.loc 5 139 0
	ldr	r3, [r4, #3960]
	cmp	r3, #0
	bne	.L1438
	.loc 5 140 0
	ldr	r3, [r4, #2708]
	.loc 5 141 0
	ldr	r0, [fp, #-44]
	.loc 5 140 0
	add	r3, r3, #1
	.loc 5 141 0
	ubfx	r0, r0, #10, #16
	.loc 5 140 0
	str	r3, [r4, #2708]
	.loc 5 141 0
	bl	FtlBbmMapBadBlock
.LVL1244:
	.loc 5 142 0
	bl	FtlBbmTblFlush
.LVL1245:
.L1438:
	.loc 5 144 0
	mov	r3, #0
	str	r3, [r4, #3960]
.L1449:
	.loc 5 149 0
	mov	r0, #1
	.loc 5 199 0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1246:
.L1434:
	.loc 5 124 0
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 125 0
	add	r3, r4, #2320
	ldrh	r3, [r3]
	cmp	r3, #17
	bhi	.L1435
	b	.L1433
.LVL1247:
.L1436:
	.loc 5 148 0
	ldr	r3, .L1455+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1449
	b	.L1440
.LVL1248:
.L1453:
	.loc 5 160 0
	bl	FtlCacheWriteBack
.LVL1249:
	.loc 5 162 0
	movw	r3, #3176
	ldrb	r1, [r4, #2427]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	ldrh	r3, [r4, #78]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1441
	.loc 5 162 0 is_stmt 0 discriminator 1
	mov	r2, #162
	ldr	r1, .L1455+12
	ldr	r0, .L1455+16
	bl	printk
.LVL1250:
.L1441:
	.loc 5 163 0 is_stmt 1
	ldrb	r2, [r4, #2427]	@ zero_extendqisi2
	movw	r3, #2420
	ldrh	r0, [r4, #78]
	.loc 5 165 0
	mov	r6, #0
	.loc 5 163 0
	ldrh	r3, [r4, r3]
	ldr	r1, [r4, #2300]
	.loc 5 165 0
	ldr	r9, .L1455+20
	.loc 5 163 0
	smulbb	r2, r2, r0
	.loc 5 168 0
	ldr	r10, .L1455+12
	.loc 5 163 0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	.loc 5 164 0
	movw	r3, #3176
	ldr	r2, [r4, #2504]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2504]
.LVL1251:
.L1442:
	.loc 5 165 0 discriminator 1
	ldrh	r2, [r9]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1446
	.loc 5 181 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1252:
	.loc 5 182 0
	movw	r3, #2420
	ldr	r1, [r4, #2300]
	ldrh	r0, [r4, r3]
	mov	r5, r3
.LVL1253:
	lsl	r2, r0, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1447
	.loc 5 183 0
	bl	INSERT_DATA_LIST
.LVL1254:
.L1448:
	.loc 5 188 0
	ldr	r6, .L1455+24
	.loc 5 186 0
	mvn	r7, #0
	.loc 5 187 0
	movw	r3, #3176
	.loc 5 186 0
	strh	r7, [r4, r5]	@ movhi
	.loc 5 187 0
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	.loc 5 188 0
	strh	r5, [r6]	@ movhi
	.loc 5 191 0
	sub	r6, r6, #848
	.loc 5 189 0
	bl	l2p_flush
.LVL1255:
	.loc 5 190 0
	bl	FtlVpcTblFlush
.LVL1256:
	.loc 5 191 0
	movw	r3, #2548
	ldrh	r2, [r6]
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	.loc 5 192 0
	movwgt	r3, #2564
	.loc 5 193 0
	movgt	r2, #20
	.loc 5 192 0
	strhgt	r7, [r4, r3]	@ movhi
	.loc 5 193 0
	movwgt	r3, #3124
	strhgt	r2, [r4, r3]	@ movhi
	b	.L1431
.LVL1257:
.L1446:
	.loc 5 166 0
	uxth	r3, r6
	mov	r7, #12
	ldr	r8, [r4, #3172]
	mul	r7, r7, r3
	.loc 5 168 0
	ldr	r3, [r4, #2480]
	.loc 5 166 0
	add	r5, r8, r7
	.loc 5 168 0
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1443
	.loc 5 168 0 is_stmt 0 discriminator 1
	mov	r2, #168
	mov	r1, r10
	ldr	r0, .L1455+16
	bl	printk
.LVL1258:
.L1443:
	.loc 5 169 0 is_stmt 1
	mov	r2, #0
	sub	r1, fp, #44
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1259:
	.loc 5 170 0
	ldr	r3, [fp, #-44]
	ldr	r0, [r8, r7]
	cmp	r0, r3
	bne	.L1444
	.loc 5 171 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1260:
	.loc 5 172 0
	mov	r2, #1
	.loc 5 171 0
	mov	r7, r0
	.loc 5 172 0
	add	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1261:
	.loc 5 173 0
	mov	r0, r7
.L1454:
	.loc 5 177 0
	bl	decrement_vpc_count
.LVL1262:
.L1445:
	add	r6, r6, #1
	b	.L1442
.L1444:
	.loc 5 175 0
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1445
	.loc 5 177 0
	ldr	r3, .L1455+8
	ldrh	r0, [r3]
	b	.L1454
.LVL1263:
.L1447:
	.loc 5 185 0
	bl	INSERT_FREE_LIST
.LVL1264:
	b	.L1448
.L1456:
	.align	2
.L1455:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2420
	.word	.LANCHOR1+532
	.word	.LC1
	.word	.LANCHOR0+3176
	.word	.LANCHOR0+3168
.LFE258:
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
.LFB261:
	.loc 5 289 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI395:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI396:
	sub	fp, ip, #4
.LCFI397:
	.loc 5 290 0
	ldr	r4, .L1459
	ldr	r5, .L1459+4
	ldrh	r1, [r4, #78]
	mov	r0, r5
	bl	FtlGcScanTempBlk
.LVL1265:
	.loc 5 291 0
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #78]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
.LBB515:
.LBB516:
	.loc 5 293 0
	ldr	r0, .L1459+8
	bl	FtlMapBlkWriteDump_data
.LVL1266:
	.loc 5 294 0
	mov	r0, #0
	bl	FtlGcFreeTempBlock
.LVL1267:
	.loc 5 295 0
	mov	r3, #0
	str	r3, [r4, #3960]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1460:
	.align	2
.L1459:
	.word	.LANCHOR0
	.word	.LANCHOR0+2420
	.word	.LANCHOR0+3864
.LBE516:
.LBE515:
.LFE261:
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
.LFB233:
	.loc 2 2081 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI398:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI399:
	sub	fp, ip, #4
.LCFI400:
	.loc 2 2082 0
	ldr	r4, .L1462
	mov	r6, #0
	.loc 2 2084 0
	add	r5, r4, #2320
	.loc 2 2082 0
	str	r6, [r4, #3972]
	.loc 2 2084 0
	add	r5, r5, #4
	.loc 2 2086 0
	add	r4, r4, #2368
	.loc 2 2084 0
	mov	r0, r5
	.loc 2 2086 0
	add	r4, r4, #4
	.loc 2 2084 0
	bl	FtlRecoverySuperblock
.LVL1268:
	.loc 2 2085 0
	mov	r0, r5
	bl	FtlSlcSuperblockCheck
.LVL1269:
	.loc 2 2086 0
	mov	r0, r4
	bl	FtlRecoverySuperblock
.LVL1270:
	.loc 2 2087 0
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
.LVL1271:
	.loc 2 2088 0
	bl	FtlGcPageRecovery
.LVL1272:
	.loc 2 2089 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1273:
	.loc 2 2091 0
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1463:
	.align	2
.L1462:
	.word	.LANCHOR0
.LFE233:
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
.LFB263:
	.loc 5 316 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI401:
	push	{r4, r5, r6, fp, ip, lr, pc}
.LCFI402:
	sub	fp, ip, #4
.LCFI403:
	.loc 5 320 0
	ldr	r4, .L1471
	mov	r3, #0
	.loc 5 321 0
	mov	r5, #0
	.loc 5 322 0
	mov	r6, #20
	.loc 5 320 0
	mov	r2, r3
	ldr	r1, [r4, #3140]
	ldr	r0, [r4, #3796]
	bl	FlashProgPages
.LVL1274:
.L1465:
	.loc 5 321 0 discriminator 1
	ldr	r1, [r4, #3140]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1468
	.loc 5 335 0
	ldr	r0, [r4, #3796]
	bl	FtlGcBufFree
.LVL1275:
	.loc 5 337 0
	ldr	r3, .L1471+4
	.loc 5 336 0
	mov	r0, #0
	str	r0, [r4, #3140]
	.loc 5 337 0
	ldrh	r3, [r3, #4]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, fp, sp, pc}
	.loc 5 338 0
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1276:
	b	.L1470
.L1468:
	.loc 5 322 0
	mul	r3, r6, r3
	ldr	r2, [r4, #3796]
	add	r5, r5, #1
	ldr	r0, [r2, r3]
	add	r1, r2, r3
	cmn	r0, #1
	bne	.L1466
	.loc 5 323 0
	movw	r1, #2420
	ldr	ip, [r4, #2300]
	ldrh	r2, [r4, r1]
	mov	lr, #0
	lsl	r2, r2, #1
	strh	lr, [ip, r2]	@ movhi
	.loc 5 325 0
	ldr	r2, [r4, #2708]
	.loc 5 324 0
	strh	r0, [r4, r1]	@ movhi
	.loc 5 325 0
	add	r2, r2, #1
	str	r2, [r4, #2708]
	.loc 5 326 0
	ldr	r2, [r4, #3796]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL1277:
	.loc 5 327 0
	bl	FtlBbmTblFlush
.LVL1278:
	.loc 5 328 0
	bl	FtlGcPageVarInit
.LVL1279:
.L1470:
	.loc 5 339 0
	mov	r0, #1
	.loc 5 342 0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1466:
	.loc 5 331 0 discriminator 2
	ldr	r3, [r1, #12]
.LVL1280:
	.loc 5 332 0 discriminator 2
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL1281:
	b	.L1465
.L1472:
	.align	2
.L1471:
	.word	.LANCHOR0
	.word	.LANCHOR0+2420
.LFE263:
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
.LFB262:
	.loc 5 300 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI404:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI405:
	sub	fp, ip, #4
.LCFI406:
	.loc 5 301 0
	ldr	r4, .L1476
	movw	r3, #2420
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1474
	.loc 5 301 0 is_stmt 0 discriminator 1
	ldr	r3, .L1476+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1475
.L1474:
	.loc 5 302 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1282:
	.loc 5 303 0
	mov	r0, #0
	.loc 5 304 0
	mov	r5, #0
	.loc 5 303 0
	bl	FtlGcFreeTempBlock
.LVL1283:
	.loc 5 305 0
	ldr	r0, .L1476+4
	.loc 5 304 0
	strb	r5, [r4, #2428]
	.loc 5 305 0
	bl	allocate_data_superblock
.LVL1284:
	.loc 5 306 0
	ldr	r3, .L1476+8
	strh	r5, [r3]	@ movhi
	.loc 5 307 0
	movw	r3, #3176
	strh	r5, [r4, r3]	@ movhi
	.loc 5 308 0
	bl	l2p_flush
.LVL1285:
	.loc 5 309 0
	mov	r0, r5
	bl	FtlEctTblFlush
.LVL1286:
	.loc 5 310 0
	bl	FtlVpcTblFlush
.LVL1287:
.L1475:
	.loc 5 312 0
	ldr	r0, .L1476+4
	bl	get_new_active_ppa
.LVL1288:
	.loc 5 313 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1477:
	.align	2
.L1476:
	.word	.LANCHOR0
	.word	.LANCHOR0+2420
	.word	.LANCHOR0+3168
.LFE262:
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
.LFB269:
	.loc 5 470 0
	@ args = 0, pretend = 0, frame = 28
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1289:
	mov	ip, sp
.LCFI407:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI408:
	sub	fp, ip, #4
.LCFI409:
	sub	sp, sp, #36
	.loc 5 470 0
	mov	r6, r0
	.loc 5 478 0
	ldr	r3, .L1591
	ldr	r0, [r3, #3780]
.LVL1290:
	mov	r5, r3
	cmp	r0, #0
	.loc 5 479 0
	movne	r0, #0
	.loc 5 478 0
	bne	.L1478
	.loc 5 481 0
	movw	r2, #2312
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	bls	.L1478
	.loc 5 485 0
	ldr	r2, .L1591+4
	ldrh	r1, [r2, #4]
.LVL1291:
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1480
.L1483:
.LBB520:
.LBB521:
	.loc 5 489 0
	movw	r3, #3180
	movw	r0, #65535
	ldrh	r1, [r5, r3]
	cmp	r1, r0
	bne	.L1481
.L1482:
	.loc 5 496 0
	ldr	r3, [r5, #3128]
	.loc 5 497 0
	movw	r2, #2564
	ldrh	r2, [r5, r2]
	movw	r4, #65535
	.loc 5 496 0
	add	r3, r3, #1
	add	r3, r3, r6, lsl #7
	.loc 5 497 0
	cmp	r2, r4
	.loc 5 496 0
	str	r3, [r5, #3128]
	.loc 5 497 0
	bne	.L1484
	movw	r1, #2420
	ldrh	r4, [r5, r1]
	cmp	r4, r2
	.loc 5 473 0
	movne	r4, r2
	.loc 5 497 0
	bne	.L1484
	movw	r2, #3178
	ldrh	r8, [r5, r2]
	cmp	r8, r4
	bne	.L1484
	.loc 5 498 0
	ldr	r7, .L1591+8
	ldrh	r2, [r7]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	.loc 5 473 0
	movls	r4, r8
	.loc 5 498 0
	bls	.L1484
.LBB522:
	.loc 5 502 0
	ldr	r3, .L1591+12
	.loc 5 501 0
	mov	r10, #0
	str	r10, [r5, #3128]
	.loc 5 502 0
	strh	r10, [r3, #-4]	@ movhi
	.loc 5 503 0
	bl	GetSwlReplaceBlock
.LVL1292:
	.loc 5 504 0
	cmp	r0, r8
	.loc 5 503 0
	mov	r4, r0
	.loc 5 504 0
	movne	r8, r0
	bne	.L1486
	.loc 5 505 0
	movw	r9, #3126
	ldrh	r2, [r7]
	ldrh	r3, [r5, r9]
	cmp	r2, r3
	bcs	.L1487
	.loc 5 506 0
	mov	r0, #64
	bl	List_get_gc_head_node
.LVL1293:
	.loc 5 507 0
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1489
	.loc 5 508 0
	mov	r0, r3
	ldr	r3, [r5, #2300]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1490
	.loc 5 509 0
	mov	r0, r10
	bl	List_get_gc_head_node
.LVL1294:
	uxth	r8, r0
	.loc 5 510 0
	mov	r3, #128
	strh	r3, [r5, r9]	@ movhi
	.loc 5 520 0
	cmp	r8, r4
	beq	.L1489
.L1486:
	.loc 5 521 0
	movw	r0, #3124
	ldr	r3, [r5, #2300]
	ldrh	r0, [r5, r0]
	lsl	r1, r8, #1
	ldrh	r2, [r7]
	mov	r4, r8
	ldrh	r3, [r3, r1]
	str	r0, [sp, #4]
	ldr	r0, [r5, #2304]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1591+16
	str	r1, [sp]
	mov	r1, r8
	bl	printk
.LVL1295:
	b	.L1489
.L1480:
.LBE522:
.LBE521:
.LBE520:
	.loc 5 485 0 discriminator 1
	movw	r1, #2420
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1483
	.loc 5 486 0
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1296:
	cmp	r0, #0
	beq	.L1483
	.loc 5 487 0
	mov	r0, #1
	b	.L1478
.L1481:
.LBB527:
.LBB525:
	.loc 5 489 0
	movw	r2, #3178
	ldrh	ip, [r5, r2]
	cmp	ip, r0
	.loc 5 490 0
	strheq	r1, [r5, r2]	@ movhi
	.loc 5 491 0
	mvneq	r2, #0
	strheq	r2, [r5, r3]	@ movhi
	b	.L1482
.L1490:
.LBB523:
	.loc 5 512 0
	mov	r3, #64
.L1583:
	.loc 5 516 0
	strh	r3, [r5, r9]	@ movhi
.L1489:
	.loc 5 523 0
	bl	FtlGcReFreshBadBlk
.LVL1297:
.L1484:
.LBE523:
	.loc 5 527 0
	movw	r2, #65535
	sub	r3, r4, r2
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r6, #0
	movne	r6, #0
	andeq	r6, r3, #1
.LVL1298:
	cmp	r6, #0
	beq	.L1492
	.loc 5 528 0
	ldr	r3, .L1591+8
	ldrh	r3, [r3]
	cmp	r3, #24
	.loc 5 529 0
	movhi	r6, #1
.LVL1299:
	.loc 5 528 0
	bhi	.L1493
	ldrh	r6, [r5, #78]
.LVL1300:
	.loc 5 530 0
	cmp	r3, #16
	.loc 5 531 0
	lsrhi	r6, r6, #5
.LVL1301:
	.loc 5 530 0
	bhi	.L1493
	.loc 5 532 0
	cmp	r3, #12
	.loc 5 533 0
	lsrhi	r6, r6, #4
.LVL1302:
	.loc 5 532 0
	bhi	.L1493
	.loc 5 534 0
	cmp	r3, #8
	.loc 5 535 0
	lsrhi	r6, r6, #2
.LVL1303:
.L1493:
	.loc 5 539 0
	movw	r2, #3124
	ldrh	r1, [r5, r2]
	cmp	r1, r3
	bcs	.L1497
	.loc 5 540 0
	movw	r3, #2420
	movw	r1, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1498
	movw	r1, #3178
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	bne	.L1498
	.loc 5 542 0
	ldr	r3, .L1591+12
	ldrh	r0, [r3, #-4]
	cmp	r0, #0
	bne	.L1499
	ldr	r3, [r5, #2480]
	ldr	r1, [r5, #2476]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	.loc 5 545 0
	movcs	r3, #18
	.loc 5 542 0
	bcs	.L1584
.L1499:
	.loc 5 543 0
	movw	r3, #2548
	movw	r2, #3124
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1584:
	.loc 5 545 0
	strh	r3, [r5, r2]	@ movhi
	.loc 5 546 0
	mov	r3, #0
	str	r3, [r5, #3136]
.LVL1304:
.L1478:
.LBE525:
.LBE527:
	.loc 5 754 0
	sub	sp, fp, #40
.LCFI410:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1305:
.L1487:
.LCFI411:
.LBB528:
.LBB526:
.LBB524:
	.loc 5 516 0
	mov	r3, #80
	b	.L1583
.LVL1306:
.L1498:
.LBE524:
	.loc 5 549 0
	movw	r3, #2548
	movw	r2, #3124
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5, r2]	@ movhi
.L1497:
	.loc 5 551 0
	movw	r3, #3182
	movw	r4, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	.loc 5 552 0
	addne	r6, r6, #32
.LVL1307:
	uxthne	r6, r6
.LVL1308:
.L1503:
	.loc 5 595 0
	movw	r3, #2564
	ldrh	r2, [r5, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1512
	.loc 5 596 0
	cmp	r4, r2
	.loc 5 597 0
	strhne	r4, [r5, r3]	@ movhi
	.loc 5 596 0
	bne	.L1514
	.loc 5 598 0
	movw	r2, #3178
	ldrh	r3, [r5, r2]
	cmp	r3, r4
	beq	.L1514
	.loc 5 600 0
	ldr	r1, [r5, #2300]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	.loc 5 601 0
	mvneq	r3, #0
	strheq	r3, [r5, r2]	@ movhi
	.loc 5 602 0
	movw	r3, #3178
	ldrh	r1, [r5, r3]
	movw	r2, #2564
	strh	r1, [r5, r2]	@ movhi
	.loc 5 603 0
	mvn	r2, #0
	strh	r2, [r5, r3]	@ movhi
.L1514:
	.loc 5 607 0
	movw	r7, #2564
	.loc 5 606 0
	mov	r3, #0
	.loc 5 607 0
	ldrh	r0, [r5, r7]
	.loc 5 606 0
	strb	r3, [r5, #2572]
	.loc 5 607 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1512
	.loc 5 608 0
	bl	IsBlkInGcList
.LVL1309:
	cmp	r0, #0
	.loc 5 611 0
	ldr	r8, .L1591+20
	.loc 5 609 0
	mvnne	r3, #0
	strhne	r3, [r5, r7]	@ movhi
	.loc 5 611 0
	movw	r7, #2564
	ldrh	r2, [r5, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1512
	.loc 5 612 0
	mov	r0, r8
	bl	make_superblock
.LVL1310:
	.loc 5 615 0
	ldr	r2, .L1591+12
	.loc 5 613 0
	mov	r3, #0
	strh	r3, [r8, #2]	@ movhi
	.loc 5 614 0
	strb	r3, [r5, #2570]
	.loc 5 615 0
	strh	r3, [r2, #-2]	@ movhi
	.loc 5 616 0
	ldrh	r3, [r5, r7]
	ldr	r1, [r5, #2300]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	strh	r3, [r2]	@ movhi
.L1512:
	.loc 5 621 0
	movw	r3, #2564
	movw	r2, #2324
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	beq	.L1518
	movw	r2, #2372
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	.loc 5 627 0
	ldrne	r7, .L1591
	movne	r5, r7
	.loc 5 621 0
	bne	.L1519
.L1518:
	.loc 5 622 0
	mvn	r2, #0
	movw	r3, #2564
.LVL1311:
.L1589:
	.loc 5 647 0
	strh	r2, [r5, r3]	@ movhi
	.loc 5 648 0
	ldr	r3, .L1591+12
.LVL1312:
.L1590:
	ldrh	r0, [r3, #-4]
	b	.L1478
.LVL1313:
.L1492:
	.loc 5 557 0
	movw	r1, #2420
	ldrh	r8, [r5, r1]
	cmp	r8, r2
	bne	.L1561
	movw	r2, #3178
	ldrh	r2, [r5, r2]
	cmp	r2, r8
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1561
	movw	r3, #2564
	ldrh	r3, [r5, r3]
	cmp	r3, r8
	beq	.L1504
.L1509:
	.loc 5 556 0
	mov	r4, r8
.L1561:
	mov	r6, #1
.LVL1314:
	b	.L1503
.LVL1315:
.L1504:
	.loc 5 560 0
	ldr	r7, .L1591+8
	movw	r2, #3124
	ldrh	r3, [r5, r2]
	.loc 5 558 0
	str	r6, [r5, #3136]
	.loc 5 560 0
	ldrh	r9, [r7]
	ldr	r4, .L1591+12
	cmp	r3, r9
	bcs	.L1505
	.loc 5 561 0
	ldrh	r3, [r4, #-4]
	cmp	r3, #0
	bne	.L1506
	ldr	r3, [r5, #2480]
	ldr	r1, [r5, #2476]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	.loc 5 564 0
	movcs	r3, #18
	.loc 5 561 0
	bcs	.L1585
.L1506:
	.loc 5 562 0
	movw	r3, #2548
	movw	r2, #3124
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1585:
	.loc 5 564 0
	strh	r3, [r5, r2]	@ movhi
.L1588:
	.loc 5 578 0
	bl	FtlReadRefresh
.LVL1316:
	.loc 5 579 0
	ldrh	r0, [r4, #-4]
	b	.L1478
.L1505:
	.loc 5 572 0
	ldrh	r0, [r4, #-4]
	cmp	r0, #0
	bne	.L1509
	.loc 5 573 0
	movw	r3, #2548
	ldrh	r6, [r5, r3]
	add	r3, r6, r6, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5, r2]	@ movhi
	.loc 5 574 0
	bl	List_get_gc_head_node
.LVL1317:
	.loc 5 575 0
	uxth	r0, r0
	ldr	r3, [r5, #2300]
	ldrh	r1, [r5, #80]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldrh	r3, [r5, #8]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1510
	.loc 5 576 0
	sub	r6, r6, #1
	.loc 5 575 0
	cmp	r9, r6
	bge	.L1588
.L1510:
	.loc 5 584 0
	cmp	r2, #0
	bne	.L1509
	.loc 5 585 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1318:
	.loc 5 586 0
	ldrh	r0, [r7]
	add	r0, r0, #1
	b	.L1478
.LVL1319:
.L1522:
	uxth	r2, r0
	str	r1, [fp, #-52]
	add	r10, r10, #1
	.loc 5 636 0
	mov	r0, r2
	str	r2, [fp, #-48]
	bl	IsBlkInGcList
.LVL1320:
	cmp	r0, #0
	ldr	r2, [fp, #-48]
	ldr	r1, [fp, #-52]
	.loc 5 637 0
	strhne	r10, [r9]	@ movhi
	.loc 5 636 0
	bne	.L1521
	.loc 5 641 0
	ldrh	lr, [r5, #8]
	.loc 5 640 0
	uxth	r10, r10
	.loc 5 641 0
	ldrh	r3, [r5, #78]
	lsl	r2, r2, #1
	ldr	r0, [r5, #2300]
	.loc 5 640 0
	strh	r10, [r9]	@ movhi
	.loc 5 641 0
	mul	r3, lr, r3
	ldrh	ip, [r0, r2]
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1525
	.loc 5 642 0
	cmp	ip, #8
	cmphi	r10, #48
	bls	.L1526
	ldr	ip, .L1591+24
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1526
.L1525:
	.loc 5 643 0
	ldr	lr, .L1591+28
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L1526:
	.loc 5 645 0
	ldrh	ip, [r0, r2]
	movw	r0, #65535
	cmp	r3, ip
	cmple	r4, r0
	moveq	r3, #1
	movne	r3, #0
	bne	.L1527
	.loc 5 646 0
	mvn	r2, #0
	movw	r3, #2564
	strh	r2, [r5, r3]	@ movhi
	.loc 5 647 0
	mov	r2, #0
	movw	r3, #3132
	b	.L1589
.L1527:
	.loc 5 651 0
	cmp	ip, #0
	bne	.L1528
	.loc 5 652 0
	bl	decrement_vpc_count
.LVL1321:
	.loc 5 653 0
	ldr	r3, .L1591+28
	ldr	r2, .L1591+28
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L1521
.L1528:
	.loc 5 656 0
	strb	r3, [r5, #2572]
	.loc 5 658 0
	ldr	r3, .L1591+32
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L1529
	movw	r2, #658
	ldr	r1, .L1591+36
	ldr	r0, .L1591+40
	bl	printk
.LVL1322:
.L1529:
	.loc 5 659 0
	ldr	r3, .L1591+44
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1530
	movw	r2, #659
	ldr	r1, .L1591+36
	ldr	r0, .L1591+40
	bl	printk
.LVL1323:
.L1530:
	.loc 5 660 0
	ldr	r3, .L1591+48
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1531
	mov	r2, #660
	ldr	r1, .L1591+36
	ldr	r0, .L1591+40
	bl	printk
.LVL1324:
.L1531:
	.loc 5 661 0
	ldr	r0, .L1591+20
	bl	make_superblock
.LVL1325:
	.loc 5 663 0
	ldrh	r2, [r8]
	.loc 5 662 0
	mov	r3, #0
	ldr	r1, .L1591+12
	.loc 5 663 0
	ldr	r0, [r5, #2300]
	lsl	r2, r2, #1
	.loc 5 662 0
	strh	r3, [r1, #-2]	@ movhi
	.loc 5 663 0
	ldrh	r2, [r0, r2]
	.loc 5 664 0
	strh	r3, [r8, #2]	@ movhi
	.loc 5 665 0
	strb	r3, [r5, #2570]
	.loc 5 663 0
	strh	r2, [r1]	@ movhi
.LVL1326:
.L1520:
	.loc 5 671 0
	bl	FtlReadRefresh
.LVL1327:
	.loc 5 674 0
	mov	r3, #1
	str	r3, [r5, #3780]
	.loc 5 675 0
	ldrh	r3, [r5, #78]
.LVL1328:
	str	r3, [fp, #-56]
	.loc 5 676 0
	ldrh	r3, [r8, #2]
.LVL1329:
	ldr	r1, [fp, #-56]
.LVL1330:
	add	r2, r3, r6
	cmp	r2, r1
	.loc 5 677 0
	movgt	r2, r1
	subgt	r6, r2, r3
.LVL1331:
	.loc 5 689 0
	mov	r3, #0
	.loc 5 677 0
	uxthgt	r6, r6
.LVL1332:
.L1587:
	str	r3, [fp, #-52]
	.loc 5 679 0
	ldrh	r3, [fp, #-52]
	ldr	r8, .L1591+20
.LVL1333:
	cmp	r6, r3
	bls	.L1541
	.loc 5 684 0
	ldrh	r9, [r8, #2]
	mov	r2, #0
	ldr	r3, [fp, #-52]
	.loc 5 683 0
	movw	r10, #65535
.LVL1334:
	.loc 5 681 0
	ldrh	ip, [r7, #8]
	.loc 5 684 0
	ldr	r0, [r7, #3156]
	add	r9, r9, r3
	add	r3, r8, #14
	mov	r8, r2
.LVL1335:
	b	.L1542
.LVL1336:
.L1535:
	.loc 5 682 0
	ldrh	r1, [r3, #2]!
.LVL1337:
	add	r2, r2, #1
	.loc 5 683 0
	cmp	r1, r10
	.loc 5 684 0
	movne	lr, #20
	orrne	r1, r9, r1, lsl #10
.LVL1338:
	mlane	lr, lr, r8, r0
	.loc 5 685 0
	addne	r8, r8, #1
.LVL1339:
	uxthne	r8, r8
.LVL1340:
	.loc 5 684 0
	strne	r1, [lr, #4]
.LVL1341:
.L1542:
	.loc 5 681 0
	uxth	r1, r2
.LVL1342:
	cmp	ip, r1
	bhi	.L1535
	.loc 5 689 0
	ldrb	r2, [r7, #2572]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.LVL1343:
	mov	r3, #0
.LVL1344:
.L1586:
	str	r3, [fp, #-48]
	.loc 5 691 0
	ldrh	r3, [fp, #-48]
	cmp	r3, r8
	ldrcs	r3, [fp, #-52]
	addcs	r3, r3, #1
	bcs	.L1587
.L1540:
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	.loc 5 692 0
	ldr	r3, [r7, #3156]
	add	r2, r3, r9
	.loc 5 693 0
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1537
	.loc 5 692 0
	ldr	r10, [r2, #12]
	.loc 5 693 0
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1537
	.loc 5 695 0
	ldr	r3, [r10, #8]
.LVL1345:
	.loc 5 696 0
	cmn	r3, #1
	bne	.L1538
	mov	r2, #696
	ldr	r1, .L1591+36
	ldr	r0, .L1591+40
	str	r3, [fp, #-60]
	bl	printk
.LVL1346:
	ldr	r3, [fp, #-60]
.LVL1347:
.L1538:
	.loc 5 697 0
	mov	r2, #0
	mov	r0, r3
	sub	r1, fp, #44
	bl	log2phys
.LVL1348:
	.loc 5 698 0
	ldr	r0, [r5, #3156]
	ldr	r3, [fp, #-44]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1537
	.loc 5 700 0
	ldr	r3, .L1591+52
	.loc 5 701 0
	mov	r1, #20
	ldr	lr, [r5, #3140]
	ldr	r0, [r0, #16]
	.loc 5 700 0
	ldrh	r2, [r3]
	.loc 5 701 0
	str	r1, [fp, #-64]
	.loc 5 700 0
	add	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	.loc 5 701 0
	ldr	r2, [r5, #3796]
	.loc 5 700 0
	str	r3, [fp, #-68]
	.loc 5 701 0
	mla	r2, r1, lr, r2
	str	r0, [r2, #16]
	str	r2, [fp, #-60]
	.loc 5 702 0
	bl	Ftl_get_new_temp_ppa
.LVL1349:
	ldr	r2, [fp, #-60]
	.loc 5 703 0
	ldr	r1, [fp, #-64]
	.loc 5 702 0
	str	r0, [r2, #4]
	.loc 5 703 0
	ldr	r0, [r5, #3140]
	ldr	r2, [r5, #3796]
	mla	r1, r1, r0, r2
	ldr	r2, [r5, #3156]
	add	r2, r2, r9
	ldr	r0, [r2, #8]
	str	r0, [r1, #8]
	.loc 5 704 0
	ldr	r2, [r2, #12]
	str	r2, [r1, #12]
	.loc 5 705 0
	ldr	r2, [fp, #-44]
.LVL1350:
	str	r2, [r10, #12]
	.loc 5 706 0
	ldr	r2, .L1591+48
.LVL1351:
	ldrh	r1, [r2]
	str	r2, [fp, #-60]
	strh	r1, [r10, #2]	@ movhi
	.loc 5 709 0
	ldr	r1, [r5, #2520]
	.loc 5 712 0
	ldr	r0, [r5, #3156]
	.loc 5 709 0
	str	r1, [r10, #4]
	.loc 5 711 0
	ldr	r1, [r5, #3140]
	.loc 5 712 0
	add	r0, r0, r9
	.loc 5 711 0
	add	r1, r1, #1
	str	r1, [r5, #3140]
	.loc 5 712 0
	mov	r1, #1
	bl	FtlGcBufAlloc
.LVL1352:
	.loc 5 713 0
	ldrb	r0, [r5, #2427]	@ zero_extendqisi2
	ldr	r1, [r5, #3140]
	ldr	r3, [fp, #-68]
	cmp	r0, r1
	beq	.L1539
	ldr	r2, [fp, #-60]
	ldrh	r2, [r2, #4]
	cmp	r2, #0
	bne	.L1537
.L1539:
	str	r3, [fp, #-60]
	.loc 5 714 0
	bl	Ftl_gc_temp_data_write_back
.LVL1353:
	cmp	r0, #0
	ldr	r3, [fp, #-60]
	.loc 5 715 0
	ldrne	r2, .L1591
	movne	r1, #0
	strne	r1, [r2, #3780]
	bne	.L1590
.L1537:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L1586
.LVL1354:
.L1541:
	.loc 5 723 0
	ldrh	r3, [r8, #2]
	add	r6, r6, r3
.LVL1355:
	.loc 5 725 0
	ldr	r3, [fp, #-56]
.LVL1356:
	.loc 5 723 0
	uxth	r6, r6
	.loc 5 725 0
	cmp	r3, r6
	.loc 5 723 0
	strh	r6, [r8, #2]	@ movhi
	ldr	r6, .L1591+12
	.loc 5 725 0
	bls	.L1543
	ldrh	r2, [r6, #-2]
	ldrh	r3, [r6]
.LVL1357:
	cmp	r2, r3
	bne	.L1544
.LVL1358:
.L1543:
	.loc 5 727 0
	ldr	r3, [r7, #3140]
.LVL1359:
	cmp	r3, #0
	beq	.L1545
	.loc 5 728 0
	bl	Ftl_gc_temp_data_write_back
.LVL1360:
	cmp	r0, #0
	.loc 5 729 0
	movne	r3, #0
	.loc 5 730 0
	ldrhne	r0, [r6, #-4]
	.loc 5 729 0
	strne	r3, [r7, #3780]
	bne	.L1478
.L1545:
	.loc 5 733 0
	ldrh	r1, [r6, #-2]
	cmp	r1, #0
	bne	.L1546
	ldrh	r3, [r8]
	ldr	r2, [r7, #2300]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	cmp	r0, #0
	beq	.L1546
	.loc 5 735 0
	strh	r1, [r2, r3]	@ movhi
	.loc 5 736 0
	ldrh	r0, [r8]
	bl	update_vpc_list
.LVL1361:
	.loc 5 737 0
	bl	FtlCacheWriteBack
.LVL1362:
	.loc 5 738 0
	bl	l2p_flush
.LVL1363:
	.loc 5 739 0
	bl	FtlVpcTblFlush
.LVL1364:
.L1546:
	.loc 5 741 0
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L1544:
	.loc 5 743 0
	ldr	r3, .L1591+8
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L1547
	.loc 5 745 0
	ldrh	r6, [r7, #78]
.LVL1365:
.L1519:
	.loc 5 627 0
	ldr	r8, .L1591+20
	movw	r3, #65535
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L1520
	.loc 5 631 0
	add	r9, r8, #568
	.loc 5 628 0
	mov	r3, #0
	str	r3, [r7, #3136]
.L1521:
	.loc 5 631 0
	ldrh	r10, [r9]
.LVL1366:
	mov	r0, r10
	bl	List_get_gc_head_node
.LVL1367:
	ldr	r3, .L1591+20
	uxth	r1, r0
	strh	r1, [r3]	@ movhi
	.loc 5 632 0
	movw	r3, #65535
	cmp	r1, r3
	bne	.L1522
	.loc 5 633 0
	mov	r3, #0
	.loc 5 634 0
	mov	r0, #8
	.loc 5 633 0
	strh	r3, [r9]	@ movhi
	b	.L1478
.LVL1368:
.L1547:
	.loc 5 750 0
	ldrh	r0, [r6, #-4]
	.loc 5 748 0
	mov	r2, #0
	str	r2, [r7, #3780]
	.loc 5 750 0
	cmp	r0, r2
	.loc 5 753 0
	addeq	r0, r3, #1
	b	.L1478
.L1592:
	.align	2
.L1591:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2320
	.word	.LANCHOR3-4064
	.word	.LC103
	.word	.LANCHOR0+2564
	.word	.LANCHOR0+3168
	.word	.LANCHOR0+3132
	.word	.LANCHOR0+2324
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+2420
	.word	.LANCHOR3-4066
.LBE526:
.LBE528:
.LFE269:
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	sftl_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
.LFB270:
	.loc 5 757 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI412:
	push	{fp, ip, lr, pc}
.LCFI413:
	sub	fp, ip, #4
.LCFI414:
	.loc 5 758 0
	mov	r1, #1
	mov	r0, r1
	bl	rk_ftl_garbage_collect
.LVL1369:
	.loc 5 759 0
	ldmfd	sp, {fp, sp, pc}
.LFE270:
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
.LFB164:
	.loc 3 814 0
	@ args = 0, pretend = 0, frame = 52
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1370:
	mov	ip, sp
.LCFI415:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI416:
	sub	fp, ip, #4
.LCFI417:
	sub	sp, sp, #52
	.loc 3 814 0
	uxtb	r0, r0
	mov	r6, r1
	str	r2, [fp, #-68]
	.loc 3 824 0
	cmp	r0, #16
	.loc 3 814 0
	mov	r9, r3
	.loc 3 824 0
	bne	.L1595
	.loc 3 825 0
	mov	r2, r3
.LVL1371:
	ldr	r1, [fp, #-68]
	add	r0, r6, #256
.LVL1372:
	bl	FtlVendorPartRead
.LVL1373:
	str	r0, [fp, #-48]
.LVL1374:
.L1594:
	.loc 3 901 0
	ldr	r0, [fp, #-48]
.LVL1375:
	sub	sp, fp, #40
.LCFI418:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1376:
.L1595:
.LCFI419:
	.loc 3 830 0
	ldr	r3, [fp, #-68]
.LVL1377:
	ldr	r10, .L1628
	add	r3, r1, r3
.LVL1378:
	str	r3, [fp, #-56]
	ldr	r2, [fp, #-56]
	ldr	r3, [r10, #116]
	cmp	r2, r3
	.loc 3 831 0
	mvnhi	r3, #0
	strhi	r3, [fp, #-48]
	.loc 3 830 0
	bhi	.L1594
	.loc 3 830 0 is_stmt 0 discriminator 1
	ldr	r3, .L1628+4
	ldr	r7, [r3]
	cmn	r7, #1
	.loc 3 831 0 is_stmt 1 discriminator 1
	streq	r7, [fp, #-48]
	.loc 3 830 0 discriminator 1
	beq	.L1594
	.loc 3 833 0
	bl	FtlCacheWriteBack
.LVL1379:
	.loc 3 835 0
	ldrh	r4, [r10, #34]
	mov	r0, r6
	.loc 3 828 0
	mov	r8, #0
.LVL1380:
	.loc 3 851 0
	mov	r7, r10
	.loc 3 840 0
	mov	r5, r8
.LVL1381:
	.loc 3 835 0
	mov	r1, r4
	bl	__udivsi3
.LVL1382:
	.loc 3 836 0
	ldr	r3, [fp, #-56]
	mov	r1, r4
	.loc 3 835 0
	str	r0, [fp, #-60]
	.loc 3 836 0
	sub	r0, r3, #1
	bl	__udivsi3
.LVL1383:
	.loc 3 837 0
	ldr	r3, [fp, #-60]
.LVL1384:
	.loc 3 835 0
	ldr	r4, [fp, #-60]
.LVL1385:
	.loc 3 836 0
	str	r0, [fp, #-64]
	.loc 3 837 0
	rsb	r3, r3, #1
.LVL1386:
	.loc 3 828 0
	str	r8, [fp, #-76]
.LVL1387:
	.loc 3 837 0
	add	r3, r3, r0
.LVL1388:
	.loc 3 820 0
	str	r8, [fp, #-72]
	.loc 3 837 0
	str	r3, [fp, #-52]
	.loc 3 841 0
	ldr	r2, [fp, #-52]
.LVL1389:
	ldr	r3, [r10, #2484]
	.loc 3 815 0
	str	r8, [fp, #-48]
	.loc 3 841 0
	add	r3, r3, r2
	str	r3, [r10, #2484]
.LVL1390:
.L1597:
	.loc 3 842 0
	ldr	r3, [fp, #-52]
.LVL1391:
	cmp	r3, #0
	bne	.L1613
	.loc 3 898 0
	ldr	r3, .L1628
.LVL1392:
	movw	r2, #3182
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1614
	.loc 3 898 0 is_stmt 0 discriminator 1
	add	r3, r3, #2320
	ldrh	r3, [r3]
	cmp	r3, #31
	bhi	.L1594
.L1614:
	.loc 3 899 0 is_stmt 1
	mov	r1, #1
	mov	r0, #0
.LVL1393:
	bl	rk_ftl_garbage_collect
.LVL1394:
	b	.L1594
.LVL1395:
.L1613:
	.loc 3 843 0
	sub	r1, fp, #44
	mov	r2, #0
	mov	r0, r4
.LVL1396:
	bl	log2phys
.LVL1397:
	.loc 3 844 0
	ldr	r1, [fp, #-44]
	cmn	r1, #1
	moveq	r10, #0
.LVL1398:
	beq	.L1599
	.loc 3 851 0
	ldr	r2, [r7, #3792]
	mov	r0, #20
	.loc 3 852 0
	ldr	r3, [fp, #-60]
.LVL1399:
	ldrh	r10, [r7, #34]
.LVL1400:
	.loc 3 851 0
	mla	r2, r0, r5, r2
	.loc 3 852 0
	cmp	r4, r3
	.loc 3 851 0
	str	r1, [r2, #4]
	.loc 3 852 0
	bne	.L1603
	.loc 3 853 0
	ldr	r1, [r7, #3776]
	.loc 3 854 0
	mov	r0, r6
	.loc 3 853 0
	str	r2, [fp, #-80]
	str	r1, [r2, #8]
	.loc 3 854 0
	mov	r1, r10
	bl	__umodsi3
.LVL1401:
	ldr	r3, [fp, #-68]
.LVL1402:
	.loc 3 855 0
	sub	r1, r10, r0
	.loc 3 857 0
	ldr	r2, [fp, #-80]
	.loc 3 854 0
	str	r0, [fp, #-72]
.LVL1403:
	cmp	r3, r1
	movcs	r3, r1
.LVL1404:
	.loc 3 857 0
	cmp	r3, r10
	str	r3, [fp, #-76]
	.loc 3 858 0
	streq	r9, [r2, #8]
.LVL1405:
.L1604:
	.loc 3 867 0
	ldrh	r1, [r7, #88]
	ldr	r0, [r7, #3772]
	.loc 3 868 0
	str	r4, [r2, #16]
	.loc 3 867 0
	mul	r1, r5, r1
	.loc 3 869 0
	add	r5, r5, #1
	.loc 3 867 0
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [r2, #12]
	b	.L1602
.LVL1406:
.L1601:
	.loc 3 846 0
	mla	r0, r0, r4, r10
.LVL1407:
	.loc 3 847 0
	ldr	r2, [fp, #-56]
	cmp	r2, r0
	movhi	r2, #1
	movls	r2, #0
	cmp	r6, r0
	movhi	r2, #0
	cmp	r2, #0
	beq	.L1600
	.loc 3 848 0
	sub	r0, r0, r6
.LVL1408:
.LBB535:
.LBB536:
.LBB537:
	.loc 1 202 0
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.LVL1409:
.L1600:
.LBE537:
.LBE536:
.LBE535:
	.loc 3 845 0 discriminator 2
	add	r10, r10, #1
.L1599:
	.loc 3 845 0 is_stmt 0 discriminator 1
	ldrh	r0, [r7, #34]
.LVL1410:
	cmp	r10, r0
	bcc	.L1601
.LVL1411:
.L1602:
	.loc 3 874 0 is_stmt 1
	ldr	r3, [fp, #-52]
.LVL1412:
	.loc 3 872 0
	add	r4, r4, #1
	.loc 3 874 0
	subs	r3, r3, #1
	str	r3, [fp, #-52]
	beq	.L1606
	.loc 3 874 0 is_stmt 0 discriminator 1
	ldrh	r2, [r7, #8]
	cmp	r5, r2, lsl #2
	bne	.L1597
.L1606:
	.loc 3 875 0 is_stmt 1
	cmp	r5, #0
	beq	.L1597
	.loc 3 876 0
	mov	r2, #0
	mov	r1, r5
	ldr	r0, [r7, #3792]
	.loc 3 877 0
	mov	r10, #0
.LVL1413:
	.loc 3 876 0
	bl	FlashReadPages
.LVL1414:
	.loc 3 883 0
	lsl	r3, r8, #9
	str	r3, [fp, #-88]
	.loc 3 880 0
	ldr	r3, [fp, #-72]
.LVL1415:
	lsl	r3, r3, #9
.LVL1416:
	str	r3, [fp, #-80]
	ldr	r3, [fp, #-76]
	lsl	r3, r3, #9
	str	r3, [fp, #-84]
.LVL1417:
.L1612:
	mov	r3, #20
.LVL1418:
	.loc 3 878 0
	ldr	r2, [r7, #3792]
	mul	ip, r3, r10
	ldr	r3, [fp, #-60]
.LVL1419:
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r3, r1
	bne	.L1608
	.loc 3 879 0
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3776]
	cmp	r1, r2
	bne	.L1609
	.loc 3 880 0
	ldr	r3, [fp, #-80]
.LVL1420:
	mov	r0, r9
	ldr	r2, [fp, #-84]
	str	ip, [fp, #-92]
	add	r1, r1, r3
.LVL1421:
.L1627:
	.loc 3 883 0
	bl	ftl_memcpy
.LVL1422:
	ldr	ip, [fp, #-92]
.LVL1423:
.L1609:
	.loc 3 885 0
	ldr	r2, [r7, #3792]
	ldr	r0, [r2, ip]
	add	lr, r2, ip
	cmn	r0, #1
	.loc 3 886 0
	ldreq	r1, [r7, #2684]
	.loc 3 887 0
	streq	r0, [fp, #-48]
	.loc 3 886 0
	addeq	r1, r1, #1
	streq	r1, [r7, #2684]
	.loc 3 889 0
	ldr	r2, [r2, ip]
	cmp	r2, #256
	bne	.L1611
.LBB538:
	.loc 3 890 0
	ldr	r0, [lr, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1424:
	.loc 3 891 0
	bl	FtlGcRefreshBlock
.LVL1425:
.L1611:
.LBE538:
	.loc 3 877 0 discriminator 2
	add	r10, r10, #1
	cmp	r5, r10
	bne	.L1612
	.loc 3 894 0
	mov	r5, #0
	b	.L1597
.LVL1426:
.L1603:
	.loc 3 859 0
	ldr	r3, [fp, #-64]
.LVL1427:
	cmp	r4, r3
	bne	.L1605
	.loc 3 860 0
	ldr	r1, [r7, #3768]
	.loc 3 861 0
	ldr	r3, [fp, #-56]
.LVL1428:
	.loc 3 860 0
	str	r1, [r2, #8]
	.loc 3 861 0
	mul	r1, r10, r4
	sub	r8, r3, r1
	.loc 3 862 0
	cmp	r10, r8
	.loc 3 863 0
	subeq	r1, r1, r6
	addeq	r1, r9, r1, lsl #9
	streq	r1, [r2, #8]
	b	.L1604
.LVL1429:
.L1605:
	.loc 3 865 0
	mul	r10, r4, r10
	sub	r10, r10, r6
	add	r10, r9, r10, lsl #9
	str	r10, [r2, #8]
	b	.L1604
.LVL1430:
.L1608:
	.loc 3 881 0
	ldr	r3, [fp, #-64]
.LVL1431:
	cmp	r3, r1
	bne	.L1609
	.loc 3 882 0
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3768]
	cmp	r1, r2
	bne	.L1609
	.loc 3 883 0
	ldrh	r0, [r7, #34]
	str	ip, [fp, #-92]
	ldr	r2, [fp, #-88]
	mul	r0, r3, r0
	sub	r0, r0, r6
	add	r0, r9, r0, lsl #9
	b	.L1627
.L1629:
	.align	2
.L1628:
	.word	.LANCHOR0
	.word	.LANCHOR2
.LFE164:
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_read, %function
sftl_read:
.LFB169:
	.loc 3 1204 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1432:
	mov	ip, sp
.LCFI420:
	push	{fp, ip, lr, pc}
.LCFI421:
	sub	fp, ip, #4
.LCFI422:
	.loc 3 1205 0
	mov	r3, r2
	mov	r2, r1
.LVL1433:
	mov	r1, r0
.LVL1434:
	mov	r0, #0
.LVL1435:
	bl	FtlRead
.LVL1436:
	.loc 3 1206 0
	ldmfd	sp, {fp, sp, pc}
.LFE169:
	.size	sftl_read, .-sftl_read
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
.LFB167:
	.loc 3 962 0
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1437:
	mov	ip, sp
.LCFI423:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI424:
	sub	fp, ip, #4
.LCFI425:
	sub	sp, sp, #64
	.loc 3 962 0
	uxtb	r0, r0
	mov	r8, r1
	str	r2, [fp, #-76]
	.loc 3 970 0
	cmp	r0, #16
	.loc 3 962 0
	str	r3, [fp, #-80]
	.loc 3 970 0
	bne	.L1632
	.loc 3 971 0
	mov	r2, r3
.LVL1438:
	ldr	r1, [fp, #-76]
	add	r0, r8, #256
.LVL1439:
	bl	FtlVendorPartWrite
.LVL1440:
.L1631:
	.loc 3 1153 0
	sub	sp, fp, #40
.LCFI426:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1441:
.L1632:
.LCFI427:
	.loc 3 974 0
	ldr	r3, [fp, #-76]
.LVL1442:
	ldr	r9, .L1696
	add	r6, r1, r3
	ldr	r3, [r9, #116]
.LVL1443:
	cmp	r6, r3
	.loc 3 975 0
	mvnhi	r0, #0
.LVL1444:
	.loc 3 974 0
	bhi	.L1631
	.loc 3 974 0 is_stmt 0 discriminator 1
	ldr	r5, .L1696+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1631
	.loc 3 977 0 is_stmt 1
	ldr	r3, .L1696+8
	mov	r2, #2048
	.loc 3 978 0
	ldrh	r4, [r9, #34]
	mov	r0, r8
	.loc 3 977 0
	str	r2, [r3, #-4060]
	.loc 3 978 0
	mov	r1, r4
	bl	__udivsi3
.LVL1445:
	.loc 3 979 0
	mov	r1, r4
	.loc 3 978 0
	str	r0, [fp, #-68]
	.loc 3 979 0
	sub	r0, r6, #1
	bl	__udivsi3
.LVL1446:
	.loc 3 980 0
	ldr	r2, [fp, #-68]
.LVL1447:
	.loc 3 983 0
	ldr	r10, [r9, #3948]
	.loc 3 979 0
	str	r0, [fp, #-88]
.LVL1448:
	.loc 3 980 0
	sub	r6, r0, r2
	add	r3, r6, #1
.LVL1449:
	.loc 3 982 0
	add	r2, r6, #1
.LVL1450:
	.loc 3 980 0
	str	r3, [fp, #-72]
	.loc 3 983 0
	cmp	r10, #0
	.loc 3 982 0
	ldr	r3, [r9, #2492]
	add	r3, r3, r2
	str	r3, [r9, #2492]
	.loc 3 983 0
	beq	.L1634
	.loc 3 984 0
	ldr	r3, [r10, #16]
	ldr	r2, [fp, #-68]
.LVL1451:
	cmp	r2, r3
	beq	.L1635
	.loc 3 985 0
	bl	FtlCacheWriteBack
.LVL1452:
.L1634:
	.loc 3 1008 0
	ldr	r3, [r5, #8]
	ldr	r4, .L1696+12
.LVL1453:
	cmp	r3, #0
	beq	.L1637
	.loc 3 1008 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #4]
	.loc 3 1009 0 is_stmt 1 discriminator 1
	add	r0, r4, #48
.LVL1454:
	cmp	r3, #0
	moveq	r4, r0
.LVL1455:
.L1637:
	.loc 3 1068 0
	ldr	r5, [fp, #-68]
.LVL1456:
	.loc 3 1012 0
	ldr	r7, .L1696
.LVL1457:
.L1638:
	.loc 3 1011 0
	ldr	r3, [fp, #-72]
.LVL1458:
	cmp	r3, #0
	beq	.L1672
	.loc 3 1012 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r7, #8]
.LVL1459:
	cmp	r2, r3
	bcc	.L1639
	.loc 3 1012 0 is_stmt 0 discriminator 1
	mov	r2, #1012
	ldr	r1, .L1696+16
	ldr	r0, .L1696+20
.LVL1460:
	bl	printk
.LVL1461:
.L1639:
	.loc 3 1013 0 is_stmt 1
	ldrh	r9, [r4, #4]
.LVL1462:
	cmp	r9, #0
	bne	.L1640
	.loc 3 1014 0
	bl	FtlCacheWriteBack
.LVL1463:
	.loc 3 1016 0
	ldr	r3, .L1696+12
	ldr	r6, .L1696+4
	cmp	r4, r3
	bne	.L1641
	.loc 3 1017 0
	add	r0, r4, #48
	ldrh	r9, [r0, #4]
	cmp	r9, #0
	bne	.L1642
	.loc 3 1018 0
	bl	allocate_new_data_superblock
.LVL1464:
	.loc 3 1019 0
	str	r9, [r6, #8]
.L1642:
	.loc 3 1021 0
	ldr	r0, .L1696+12
	bl	allocate_new_data_superblock
.LVL1465:
	.loc 3 1023 0
	ldr	r4, .L1696+12
.LVL1466:
	.loc 3 1022 0
	ldr	r3, [r6, #8]
	.loc 3 1023 0
	add	r0, r4, #48
	cmp	r3, #0
	movne	r4, r0
.LVL1467:
.L1643:
	.loc 3 1033 0
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1640
	.loc 3 1034 0
	mov	r0, r4
	bl	allocate_new_data_superblock
.LVL1468:
.L1640:
	.loc 3 1043 0
	ldrh	r3, [r4, #4]
	ldr	r2, [fp, #-72]
.LVL1469:
	cmp	r3, r2
	movcs	r3, r2
	.loc 3 1041 0
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
.LVL1470:
	lsl	r2, r2, #2
	cmp	r3, r2
	movcs	r3, r2
	.loc 3 1045 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	str	r3, [fp, #-104]
	ldrh	r3, [r7, #8]
	cmp	r2, r3
	bcc	.L1644
	.loc 3 1045 0 is_stmt 0 discriminator 1
	movw	r2, #1045
	ldr	r1, .L1696+16
	ldr	r0, .L1696+20
.LVL1471:
	bl	printk
.LVL1472:
.L1644:
	.loc 3 1068 0 is_stmt 1 discriminator 1
	mov	r9, #0
.LVL1473:
.L1645:
	.loc 3 1046 0 discriminator 1
	ldr	r3, [fp, #-104]
	cmp	r9, r3
	bne	.L1666
.L1646:
	.loc 3 1127 0
	ldr	r3, [r7, #3948]
	cmp	r3, #0
	beq	.L1667
	.loc 3 1129 0
	ldr	r3, [fp, #-72]
.LVL1474:
	.loc 3 1130 0
	subs	r9, r9, #1
	.loc 3 1129 0
	sub	r3, r3, #1
	str	r3, [fp, #-72]
	.loc 3 1130 0
	bne	.L1667
.L1672:
	.loc 3 1137 0
	ldr	r3, [fp, #-88]
.LVL1475:
	mov	r0, #0
	ldr	r2, [fp, #-68]
.LVL1476:
	.loc 3 1139 0
	ldr	r4, .L1696+24
.LVL1477:
	.loc 3 1137 0
	sub	r1, r3, r2
	bl	rk_ftl_garbage_collect
.LVL1478:
	.loc 3 1139 0
	ldrh	r3, [r4]
	cmp	r3, #15
	.loc 3 1141 0
	addls	r5, r4, #244
.LVL1479:
	ldrls	r6, .L1696+28
	.loc 3 1139 0
	bhi	.L1669
.L1692:
	.loc 3 1141 0
	ldrh	r3, [r5]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1673
	.loc 3 1141 0 is_stmt 0 discriminator 1
	ldrh	r2, [r6]
	cmp	r2, r3
	bne	.L1673
	.loc 3 1142 0 is_stmt 1
	mov	r0, #0
	bl	List_get_gc_head_node
.LVL1480:
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.LVL1481:
.L1673:
	.loc 3 1143 0
	ldr	r3, .L1696+32
	.loc 3 1145 0
	mov	r1, #1
	.loc 3 1143 0
	mov	r2, #128
	.loc 3 1145 0
	mov	r0, r1
	.loc 3 1143 0
	strh	r2, [r3]	@ movhi
	.loc 3 1144 0
	strh	r2, [r3, #-2]	@ movhi
	.loc 3 1145 0
	bl	rk_ftl_garbage_collect
.LVL1482:
	.loc 3 1146 0
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1483:
	.loc 3 1147 0
	ldrh	r3, [r4]
	cmp	r3, #8
	bls	.L1692
	b	.L1669
.LVL1484:
.L1635:
	.loc 3 987 0
	ldr	r3, [r9, #2496]
	.loc 3 988 0
	mov	r1, r4
	mov	r0, r8
.LVL1485:
	.loc 3 987 0
	add	r3, r3, #1
	str	r3, [r9, #2496]
	.loc 3 988 0
	bl	__umodsi3
.LVL1486:
	ldr	r3, [fp, #-76]
.LVL1487:
	.loc 3 989 0
	sub	r4, r4, r0
	.loc 3 992 0
	ldr	r1, [fp, #-80]
	cmp	r3, r4
	movcc	r4, r3
	ldr	r3, [r10, #8]
.LVL1488:
	lsl	r9, r4, #9
	mov	r2, r9
	add	r0, r3, r0, lsl #9
	bl	ftl_memcpy
.LVL1489:
	.loc 3 994 0
	cmp	r6, #0
	bne	.L1636
.LVL1490:
.L1669:
	.loc 3 995 0
	mov	r0, #0
	b	.L1631
.LVL1491:
.L1636:
	.loc 3 996 0
	ldr	r3, [fp, #-76]
.LVL1492:
	.loc 3 997 0
	add	r8, r8, r4
	.loc 3 996 0
	sub	r3, r3, r4
	str	r3, [fp, #-76]
	.loc 3 998 0
	ldr	r3, [fp, #-80]
.LVL1493:
	add	r3, r3, r9
	str	r3, [fp, #-80]
	.loc 3 999 0
	ldr	r3, [fp, #-68]
.LVL1494:
	add	r3, r3, #1
	str	r3, [fp, #-68]
	.loc 3 1000 0
	bl	FtlCacheWriteBack
.LVL1495:
	str	r6, [fp, #-72]
	b	.L1634
.LVL1496:
.L1641:
	.loc 3 1028 0
	ldrh	r2, [r3, #4]
	.loc 3 1027 0
	str	r9, [r6, #8]
	.loc 3 1028 0
	cmp	r2, #0
	movne	r4, r3
	bne	.L1640
	.loc 3 1029 0
	mov	r0, r4
	bl	allocate_new_data_superblock
.LVL1497:
	b	.L1643
.LVL1498:
.L1666:
	.loc 3 1047 0
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1646
	.loc 3 1051 0
	mov	r2, #0
	sub	r1, fp, #64
	mov	r0, r5
.LVL1499:
	mov	r6, #20
	bl	log2phys
.LVL1500:
	.loc 3 1052 0
	mov	r0, r4
	mul	r6, r6, r9
	bl	get_new_active_ppa
.LVL1501:
	.loc 3 1055 0
	ldrh	r2, [r7, #88]
	.loc 3 1054 0
	ldr	r3, [r7, #3800]
	.loc 3 1055 0
	mul	r1, r2, r9
	.loc 3 1054 0
	add	r3, r3, r6
	str	r0, [r3, #4]
	.loc 3 1056 0
	str	r5, [r3, #16]
	.loc 3 1055 0
	bic	r1, r1, #3
	str	r1, [fp, #-96]
	ldr	r0, [fp, #-96]
	ldr	r1, [r7, #3772]
	add	r10, r1, r0
	str	r1, [fp, #-100]
	str	r10, [r3, #12]
	.loc 3 1059 0
	mov	r1, #0
	mov	r0, r10
	bl	ftl_memset
.LVL1502:
	.loc 3 1061 0
	ldr	r3, [fp, #-68]
.LVL1503:
	ldr	r2, [fp, #-88]
.LVL1504:
	cmp	r5, r2
	cmpne	r5, r3
	ldrh	r3, [r7, #34]
.LVL1505:
	bne	.L1647
	.loc 3 1062 0
	ldr	r2, [fp, #-68]
.LVL1506:
	cmp	r5, r2
	bne	.L1648
	.loc 3 1063 0
	mov	r1, r3
	mov	r0, r8
	str	r3, [fp, #-84]
	bl	__umodsi3
.LVL1507:
	.loc 3 1064 0
	ldr	r3, [fp, #-84]
	ldr	r2, [fp, #-76]
.LVL1508:
	.loc 3 1063 0
	str	r0, [fp, #-92]
	.loc 3 1064 0
	sub	r3, r3, r0
	cmp	r3, r2
	movcs	r3, r2
	str	r3, [fp, #-84]
.L1649:
	.loc 3 1072 0
	ldrh	r3, [r7, #34]
	ldr	r2, [fp, #-84]
.LVL1509:
	cmp	r2, r3
	.loc 3 1073 0
	ldr	r3, [fp, #-68]
.LVL1510:
	.loc 3 1072 0
	bne	.L1650
	.loc 3 1073 0
	cmp	r5, r3
	.loc 3 1074 0
	ldr	r3, [r7, #3800]
.LVL1511:
	add	r6, r3, r6
	ldreq	r3, [fp, #-80]
.LVL1512:
	.loc 3 1073 0
	beq	.L1694
	.loc 3 1076 0
	ldr	r3, [fp, #-84]
.LVL1513:
	mul	r2, r3, r5
	ldr	r3, [fp, #-80]
.LVL1514:
	sub	r2, r2, r8
	add	r2, r3, r2, lsl #9
	str	r2, [r6, #8]
.LVL1515:
.L1652:
	.loc 3 1119 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r7, #8]
.LVL1516:
	cmp	r2, r3
	bcc	.L1663
	.loc 3 1119 0 is_stmt 0 discriminator 1
	movw	r2, #1119
	ldr	r1, .L1696+16
	ldr	r0, .L1696+20
	bl	printk
.LVL1517:
.L1663:
	.loc 3 1120 0 is_stmt 1
	ldr	r3, .L1696+36
	.loc 3 1046 0
	add	r9, r9, #1
	.loc 3 1120 0
	ldr	r2, [fp, #-100]
	ldr	r1, [fp, #-96]
	strh	r3, [r2, r1]	@ movhi
	.loc 3 1121 0
	ldr	r3, [r7, #2520]
	.loc 3 1122 0
	str	r5, [r10, #8]
	.loc 3 1125 0
	add	r5, r5, #1
	.loc 3 1121 0
	str	r3, [r10, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r7, #2520]
	.loc 3 1123 0
	ldr	r3, [fp, #-64]
.LVL1518:
	str	r3, [r10, #12]
	.loc 3 1124 0
	ldrh	r3, [r4]
.LVL1519:
	strh	r3, [r10, #2]	@ movhi
	b	.L1645
.LVL1520:
.L1648:
	.loc 3 1069 0
	ldr	r2, [fp, #-76]
.LVL1521:
	smulbb	r3, r3, r5
	add	r2, r8, r2
.LVL1522:
	sub	r2, r2, r3
	uxth	r3, r2
	str	r3, [fp, #-84]
	.loc 3 1068 0
	mov	r3, #0
	str	r3, [fp, #-92]
	b	.L1649
.LVL1523:
.L1650:
	.loc 3 1079 0
	cmp	r5, r3
	.loc 3 1080 0
	ldr	r3, [r7, #3800]
.LVL1524:
	ldreq	r2, [r7, #3776]
	.loc 3 1082 0
	ldrne	r2, [r7, #3768]
	.loc 3 1080 0
	add	r3, r3, r6
	.loc 3 1082 0
	str	r2, [r3, #8]
	.loc 3 1084 0
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1655
.LBB539:
	.loc 3 1086 0
	str	r3, [fp, #-56]
	.loc 3 1090 0
	mov	r1, #1
	.loc 3 1088 0
	ldr	r3, [r7, #3800]
	.loc 3 1090 0
	sub	r0, fp, #60
	.loc 3 1087 0
	str	r5, [fp, #-44]
	.loc 3 1088 0
	add	r3, r3, r6
	ldr	r2, [r3, #8]
	.loc 3 1089 0
	ldr	r3, [r3, #12]
	.loc 3 1088 0
	str	r2, [fp, #-52]
	.loc 3 1090 0
	mov	r2, #0
	.loc 3 1089 0
	str	r3, [fp, #-48]
	.loc 3 1090 0
	bl	FlashReadPages
.LVL1525:
	.loc 3 1091 0
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	.loc 3 1092 0
	ldreq	r3, [r7, #2684]
	addeq	r3, r3, #1
	streq	r3, [r7, #2684]
	.loc 3 1091 0
	beq	.L1659
	.loc 3 1094 0
	ldr	r3, [r10, #8]
	cmp	r5, r3
	beq	.L1658
	.loc 3 1095 0
	ldr	r3, [r7, #2684]
	.loc 3 1096 0
	mov	r2, r5
	ldr	r0, .L1696+40
	.loc 3 1095 0
	add	r3, r3, #1
	str	r3, [r7, #2684]
	.loc 3 1096 0
	ldr	r1, [r10, #8]
	bl	printk
.LVL1526:
.L1658:
	.loc 3 1098 0
	ldr	r3, [r10, #8]
	cmp	r5, r3
	beq	.L1659
	.loc 3 1098 0 is_stmt 0 discriminator 1
	movw	r2, #1098
	ldr	r1, .L1696+16
	ldr	r0, .L1696+20
	bl	printk
.LVL1527:
.L1659:
.LBE539:
	.loc 3 1104 0 is_stmt 1
	ldr	r3, [fp, #-68]
.LVL1528:
	cmp	r5, r3
	ldr	r3, [fp, #-84]
.LVL1529:
	lsl	r2, r3, #9
	bne	.L1660
	.loc 3 1105 0
	ldr	r3, [r7, #3800]
	ldr	r1, [fp, #-80]
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	ldr	r3, [fp, #-92]
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
.LVL1530:
	.loc 3 1109 0
	ldr	r3, [fp, #-88]
.LVL1531:
	cmp	r5, r3
	bne	.L1652
.L1661:
	.loc 3 1110 0
	ldrh	r3, [r4, #4]
.LVL1532:
	cmp	r3, #0
	.loc 3 1111 0
	ldrne	r3, [r7, #3800]
	addne	r6, r3, r6
	.loc 3 1112 0
	ldrne	r3, .L1696+8
	.loc 3 1111 0
	strne	r6, [r7, #3948]
	.loc 3 1112 0
	strne	r4, [r3, #-4076]
	b	.L1652
.L1655:
	.loc 3 1101 0
	ldr	r3, [r7, #3800]
	mov	r1, #0
	ldrh	r2, [r7, #86]
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL1533:
	b	.L1659
.L1660:
	.loc 3 1107 0
	ldrh	r1, [r7, #34]
	ldr	r0, [fp, #-80]
.LVL1534:
	ldr	r3, [r7, #3800]
	mul	r1, r5, r1
	add	r3, r3, r6
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
.LVL1535:
	bl	ftl_memcpy
.LVL1536:
	b	.L1661
.LVL1537:
.L1647:
	.loc 3 1117 0
	ldr	r2, [r7, #3800]
.LVL1538:
	mul	r3, r5, r3
	add	r6, r2, r6
	ldr	r2, [fp, #-80]
.LVL1539:
	sub	r3, r3, r8
	add	r3, r2, r3, lsl #9
.L1694:
	str	r3, [r6, #8]
	b	.L1652
.LVL1540:
.L1667:
	.loc 3 1133 0
	mov	r3, r4
.LVL1541:
	mov	r2, #0
	mov	r1, r9
	ldr	r0, [r7, #3800]
.LVL1542:
	bl	FtlProgPages
.LVL1543:
	.loc 3 1134 0
	ldr	r3, [fp, #-72]
.LVL1544:
	cmp	r3, r9
	bcs	.L1670
	.loc 3 1134 0 is_stmt 0 discriminator 1
	movw	r2, #1134
	ldr	r1, .L1696+16
	ldr	r0, .L1696+20
	bl	printk
.LVL1545:
.L1670:
	.loc 3 1135 0 is_stmt 1
	ldr	r3, [fp, #-72]
	sub	r3, r3, r9
	str	r3, [fp, #-72]
	b	.L1638
.L1697:
	.align	2
.L1696:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2324
	.word	.LANCHOR1+574
	.word	.LC1
	.word	.LANCHOR0+2320
	.word	.LANCHOR0+3178
	.word	.LANCHOR0+3126
	.word	-3947
	.word	.LC104
.LFE167:
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
.LFB170:
	.loc 3 1209 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1546:
	mov	ip, sp
.LCFI428:
	push	{fp, ip, lr, pc}
.LCFI429:
	sub	fp, ip, #4
.LCFI430:
	.loc 3 1210 0
	mov	r3, r2
	mov	r2, r1
.LVL1547:
	mov	r1, r0
.LVL1548:
	mov	r0, #0
.LVL1549:
	bl	FtlWrite
.LVL1550:
	.loc 3 1211 0
	ldmfd	sp, {fp, sp, pc}
.LFE170:
	.size	sftl_write, .-sftl_write
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
.LFB224:
	.loc 2 1409 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI431:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI432:
	sub	fp, ip, #4
.LCFI433:
	.loc 2 1414 0
	ldr	r4, .L1720
	.loc 2 1417 0
	mov	r1, #0
	ldr	r6, .L1720+4
	.loc 2 1414 0
	ldr	r3, [r4, #3240]
	.loc 2 1417 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #2300]
	.loc 2 1414 0
	str	r3, [r4, #3228]
	.loc 2 1415 0
	ldr	r3, [r4, #3244]
	.loc 2 1417 0
	lsl	r2, r2, #1
	.loc 2 1415 0
	str	r3, [r4, #3232]
	.loc 2 1417 0
	bl	ftl_memset
.LVL1551:
	.loc 2 1418 0
	movw	r3, #2552
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1700
.L1708:
	.loc 2 1419 0
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1700:
	.loc 2 1427 0
	ldr	r7, .L1720+8
	.loc 2 1421 0
	mov	r1, #1
	.loc 2 1430 0
	ldr	r8, .L1720+12
	.loc 2 1421 0
	bl	FtlGetLastWrittenPage
.LVL1552:
	sxth	r5, r0
.LVL1553:
	.loc 2 1422 0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1702:
	.loc 2 1424 0
	cmp	r5, #0
	bge	.L1705
	.loc 2 1437 0
	movw	r2, #1437
	ldr	r1, .L1720+16
	ldr	r0, .L1720+20
	bl	printk
.LVL1554:
.L1704:
	.loc 2 1439 0
	ldrh	r3, [r4, #16]
	ldrh	r2, [r4, #86]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1707
	.loc 2 1439 0 is_stmt 0 discriminator 1
	movw	r2, #1439
	ldr	r1, .L1720+16
	ldr	r0, .L1720+20
	bl	printk
.LVL1555:
.L1707:
	.loc 2 1441 0 is_stmt 1
	ldr	r5, .L1720+24
.LVL1556:
	mov	r2, #48
	ldr	r1, [r4, #3228]
	mov	r0, r5
	bl	ftl_memcpy
.LVL1557:
	.loc 2 1442 0
	ldrh	r2, [r4, #16]
	ldr	r1, [r4, #3228]
	ldr	r0, [r4, #2300]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
.LVL1558:
	.loc 2 1443 0
	ldrh	r1, [r4, #16]
	ldr	r3, [r4, #3228]
	ldr	r0, [r4]
	lsr	r2, r1, #3
	add	r1, r1, #24
	lsl	r1, r1, #1
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
.LVL1559:
	.loc 2 1445 0
	ldr	r2, [r4, #2244]
	ldr	r3, .L1720+12
	cmp	r2, r3
	bne	.L1708
	.loc 2 1449 0
	ldrb	r2, [r4, #2254]	@ zero_extendqisi2
	ldrh	r3, [r4, #30]
	.loc 2 1448 0
	ldrh	r7, [r5, #8]
	.loc 2 1449 0
	cmp	r2, r3
	.loc 2 1448 0
	strh	r7, [r6, #6]	@ movhi
	.loc 2 1449 0
	bne	.L1708
	.loc 2 1452 0
	ldr	r3, .L1720+28
	.loc 2 1454 0
	ldrh	r2, [r4, #34]
	.loc 2 1455 0
	ldr	r6, [r4, #20]
	.loc 2 1452 0
	str	r7, [r3, #-4056]
	.loc 2 1453 0
	ldrh	r3, [r4, #78]
	.loc 2 1455 0
	ldrh	r0, [r4, #134]
	ldrh	r1, [r4, #8]
	.loc 2 1453 0
	mul	r3, r7, r3
	.loc 2 1455 0
	sub	r0, r6, r0
	sub	r0, r0, r7
	.loc 2 1453 0
	str	r3, [r4, #2480]
	.loc 2 1454 0
	mul	r3, r3, r2
	str	r3, [r4, #116]
	.loc 2 1455 0
	bl	__udivsi3
.LVL1560:
	.loc 2 1461 0
	cmp	r7, r6
	.loc 2 1455 0
	movw	r3, #2548
	strh	r0, [r4, r3]	@ movhi
	.loc 2 1461 0
	bls	.L1709
	.loc 2 1461 0 is_stmt 0 discriminator 1
	movw	r2, #1461
	ldr	r1, .L1720+16
	ldr	r0, .L1720+20
	bl	printk
.LVL1561:
.L1709:
	.loc 2 1463 0 is_stmt 1
	ldrh	r0, [r5, #14]
	movw	r3, #2324
	ldr	r1, .L1720+32
	.loc 2 1468 0
	mvn	ip, #0
	.loc 2 1463 0
	strh	r0, [r4, r3]	@ movhi
	.loc 2 1464 0
	ldrh	r3, [r5, #16]
	lsr	r2, r3, #6
	.loc 2 1465 0
	and	r3, r3, #63
	strb	r3, [r4, #2330]
	.loc 2 1466 0
	ldrb	r3, [r4, #2255]	@ zero_extendqisi2
	.loc 2 1464 0
	strh	r2, [r1, #2]	@ movhi
	.loc 2 1473 0
	movw	r2, #2372
	.loc 2 1466 0
	strb	r3, [r4, #2332]
	.loc 2 1468 0
	movw	r3, #2564
	strh	ip, [r4, r3]	@ movhi
	.loc 2 1469 0
	mov	r3, #0
	.loc 2 1473 0
	ldrh	ip, [r5, #18]
	.loc 2 1469 0
	strh	r3, [r1, #242]	@ movhi
	.loc 2 1470 0
	strb	r3, [r4, #2570]
	.loc 2 1473 0
	strh	ip, [r4, r2]	@ movhi
	.loc 2 1474 0
	ldrh	r2, [r5, #20]
	.loc 2 1471 0
	strb	r3, [r4, #2572]
	.loc 2 1483 0
	str	r3, [r4, #2504]
	.loc 2 1484 0
	str	r3, [r4, #2492]
	.loc 2 1474 0
	lsr	ip, r2, #6
	.loc 2 1475 0
	and	r2, r2, #63
	strb	r2, [r4, #2378]
	.loc 2 1476 0
	ldrb	r2, [r4, #2256]	@ zero_extendqisi2
	.loc 2 1474 0
	strh	ip, [r1, #50]	@ movhi
	.loc 2 1478 0
	ldrh	ip, [r5, #22]
	.loc 2 1476 0
	strb	r2, [r4, #2380]
	.loc 2 1478 0
	movw	r2, #2420
	.loc 2 1485 0
	str	r3, [r4, #2484]
	.loc 2 1478 0
	strh	ip, [r4, r2]	@ movhi
	.loc 2 1479 0
	ldrh	r2, [r5, #24]
	.loc 2 1486 0
	str	r3, [r4, #2500]
	.loc 2 1489 0
	str	r3, [r4, #2528]
	.loc 2 1479 0
	lsr	ip, r2, #6
	.loc 2 1480 0
	and	r2, r2, #63
	strb	r2, [r4, #2426]
	.loc 2 1481 0
	ldrb	r2, [r4, #2257]	@ zero_extendqisi2
	.loc 2 1479 0
	strh	ip, [r1, #98]	@ movhi
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
	strhi	r3, [r4, #2516]
	.loc 2 1496 0
	ldr	r3, [r4, #2280]
	cmp	r3, r2
	.loc 2 1497 0
	strhi	r3, [r4, #2520]
	.loc 2 1499 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1712
	.loc 2 1500 0
	ldr	r0, .L1720+32
	bl	make_superblock
.LVL1562:
.L1712:
	.loc 2 1502 0
	movw	r3, #2372
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1713
	.loc 2 1503 0
	ldr	r0, .L1720+36
	bl	make_superblock
.LVL1563:
.L1713:
	.loc 2 1505 0
	movw	r3, #2420
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1714
	.loc 2 1506 0
	ldr	r0, .L1720+40
	bl	make_superblock
.LVL1564:
.L1714:
	.loc 2 1508 0
	movw	r3, #2564
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1715
	.loc 2 1509 0
	ldr	r0, .L1720+44
	bl	make_superblock
.LVL1565:
.L1715:
	.loc 2 1511 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL1566:
.L1705:
	.loc 2 1425 0
	ldrh	r3, [r6]
	.loc 2 1427 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 2 1425 0
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3224]
	.loc 2 1426 0
	ldr	r3, [r4, #3240]
	str	r3, [r4, #3228]
	.loc 2 1427 0
	bl	FlashReadPages
.LVL1567:
	.loc 2 1430 0
	ldr	r3, [r4, #3220]
	cmn	r3, #1
	beq	.L1703
	.loc 2 1431 0 discriminator 1
	ldr	r3, [r4, #3240]
	.loc 2 1430 0 discriminator 1
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1703
	.loc 2 1432 0
	ldr	r3, [r4, #3244]
	ldrh	r2, [r3]
	.loc 2 1431 0
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1704
.L1703:
	sub	r5, r5, #1
.LVL1568:
	sxth	r5, r5
.LVL1569:
	b	.L1702
.L1721:
	.align	2
.L1720:
	.word	.LANCHOR0
	.word	.LANCHOR0+2552
	.word	.LANCHOR0+3220
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
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlSysBlkInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
.LFB242:
	.loc 2 2356 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI434:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
.LCFI435:
	sub	fp, ip, #4
.LCFI436:
	.loc 2 2360 0
	ldr	r4, .L1738
	mov	r3, #0
	add	r6, r4, #3968
	.loc 2 2362 0
	ldrh	r0, [r4, #12]
	.loc 2 2360 0
	strh	r3, [r6]	@ movhi
	.loc 2 2362 0
	bl	FtlFreeSysBlkQueueInit
.LVL1570:
	.loc 2 2363 0
	bl	FtlScanSysBlk
.LVL1571:
	.loc 2 2364 0
	movw	r3, #2552
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1723
.L1725:
	.loc 2 2365 0
	mvn	r8, #0
.L1722:
	.loc 2 2448 0
	mov	r0, r8
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1723:
	.loc 2 2367 0
	bl	FtlLoadSysInfo
.LVL1572:
	subs	r8, r0, #0
	bne	.L1725
	.loc 2 2370 0
	bl	FtlLoadMapInfo
.LVL1573:
	.loc 2 2371 0
	bl	FtlLoadVonderInfo
.LVL1574:
	.loc 2 2372 0
	bl	Ftl_load_ext_data
.LVL1575:
	.loc 2 2374 0
	bl	FtlLoadEctTbl
.LVL1576:
	.loc 2 2375 0
	bl	FtlFreeSysBLkSort
.LVL1577:
	.loc 2 2377 0
	bl	SupperBlkListInit
.LVL1578:
	.loc 2 2378 0
	bl	FtlPowerLostRecovery
.LVL1579:
	.loc 2 2380 0
	mov	r0, #1
	bl	FtlUpdateVaildLpn
.LVL1580:
.LBB543:
.LBB544:
	.loc 2 2382 0
	ldrh	r1, [r4, #114]
	mov	r3, r8
.LVL1581:
	.loc 2 2383 0
	ldr	r2, [r4, #2468]
	mov	r0, #12
.L1726:
	.loc 2 2382 0
	cmp	r3, r1
	bge	.L1731
	.loc 2 2383 0
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1727
.L1731:
	.loc 2 2386 0
	ldr	r5, .L1738+4
	.loc 2 2388 0
	cmp	r3, r1
	.loc 2 2386 0
	ldrh	r2, [r5, #28]
	add	r7, r5, #80
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	.loc 2 2388 0
	bge	.L1737
.L1728:
	.loc 2 2390 0
	ldr	r6, .L1738+8
	.loc 2 2389 0
	ldr	r0, .L1738+12
	bl	FtlSuperblockPowerLostFix
.LVL1582:
	.loc 2 2390 0
	mov	r0, r6
	bl	FtlSuperblockPowerLostFix
.LVL1583:
.LBB545:
	.loc 2 2396 0
	movw	r3, #2324
	ldr	r1, [r4, #2300]
	ldrh	r3, [r4, r3]
	ldrh	r0, [r7, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	.loc 2 2402 0
	movw	r2, #2372
	ldrh	r2, [r4, r2]
	.loc 2 2397 0
	ldrh	r3, [r4, #78]
	.loc 2 2402 0
	ldr	r0, [r4, #2300]
	ldrh	ip, [r6, #4]
	lsl	r2, r2, #1
	.loc 2 2397 0
	strh	r3, [r7, #2]	@ movhi
	.loc 2 2398 0
	mov	r3, #0
	strb	r3, [r4, #2330]
	.loc 2 2399 0
	strh	r3, [r7, #4]	@ movhi
	.loc 2 2402 0
	ldrh	r1, [r0, r2]
	sub	r1, r1, ip
	strh	r1, [r0, r2]	@ movhi
	.loc 2 2403 0
	ldrh	r2, [r4, #78]
.LBE545:
	.loc 2 2409 0
	ldr	r0, .L1738+16
.LBB546:
	.loc 2 2404 0
	strb	r3, [r4, #2378]
	.loc 2 2403 0
	strh	r2, [r6, #2]	@ movhi
	.loc 2 2405 0
	strh	r3, [r6, #4]	@ movhi
.LBE546:
	.loc 2 2409 0
	bl	FtlMapBlkWriteDump_data
.LVL1584:
	.loc 2 2410 0
	add	r0, r6, #1536
	bl	FtlMapBlkWriteDump_data
.LVL1585:
	.loc 2 2422 0
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	.loc 2 2423 0
	bl	l2p_flush
.LVL1586:
	.loc 2 2424 0
	bl	FtlVpcTblFlush
.LVL1587:
	.loc 2 2425 0
	bl	FtlVpcTblFlush
.LVL1588:
	b	.L1732
.LVL1589:
.L1727:
	.loc 2 2382 0
	add	r3, r3, #1
	b	.L1726
.L1737:
	.loc 2 2388 0
	ldrh	r3, [r6]
.LVL1590:
	cmp	r3, #0
	bne	.L1728
.L1732:
	.loc 2 2429 0
	movw	r3, #2324
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1733
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L1733
	.loc 2 2431 0
	ldr	r6, .L1738+8
	.loc 2 2430 0
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L1733
	.loc 2 2432 0
	bl	FtlGcRefreshBlock
.LVL1591:
	.loc 2 2433 0
	movw	r3, #2372
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
.LVL1592:
	.loc 2 2434 0
	sub	r0, r6, #48
	bl	allocate_new_data_superblock
.LVL1593:
	.loc 2 2435 0
	mov	r0, r6
	bl	allocate_new_data_superblock
.LVL1594:
.L1733:
	.loc 2 2440 0
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L1722
	.loc 2 2441 0
	bl	FtlVpcCheckAndModify
.LVL1595:
	b	.L1722
.L1739:
	.align	2
.L1738:
	.word	.LANCHOR0
	.word	.LANCHOR0+2244
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+3864
.LBE544:
.LBE543:
.LFE242:
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
.LFB160:
	.loc 3 623 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI437:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
.LCFI438:
	sub	fp, ip, #4
.LCFI439:
	.loc 3 629 0
	ldr	r4, .L1764
	mov	r3, #0
	.loc 3 631 0
	ldrh	r0, [r4, #12]
	.loc 3 629 0
	str	r3, [r4, #2516]
	.loc 3 630 0
	str	r3, [r4, #2520]
	.loc 3 631 0
	bl	FtlFreeSysBlkQueueInit
.LVL1596:
	.loc 3 632 0
	bl	FtlLoadBbt
.LVL1597:
	cmp	r0, #0
	beq	.L1741
	.loc 3 633 0
	bl	FtlMakeBbt
.LVL1598:
.L1741:
	.loc 3 637 0 discriminator 1
	ldr	r0, .L1764+4
	.loc 3 623 0 discriminator 1
	mov	r2, #0
.L1742:
	.loc 3 635 0 discriminator 1
	ldrh	r1, [r4, #34]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1743
	.loc 3 642 0
	ldrh	r6, [r4, #16]
	.loc 3 641 0
	mov	r5, #0
.LVL1599:
.L1744:
	.loc 3 642 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r6
	bhi	.L1745
	.loc 3 645 0
	ldrh	r1, [r4, #8]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1746
.L1750:
	.loc 3 641 0
	mov	r0, #0
	mov	r6, r0
.LVL1600:
.L1747:
	.loc 3 656 0 discriminator 1
	ldrh	r2, [r4, #16]
	uxth	r3, r0
	add	r5, r0, #1
.LVL1601:
	cmp	r2, r3
	bhi	.L1751
	.loc 3 658 0
	ldrh	r3, [r4, #18]
	.loc 3 662 0
	movw	r9, #2548
	.loc 3 661 0
	ldrh	r5, [r4, #8]
	ldr	r10, [r4, #20]
	.loc 3 658 0
	str	r3, [r4, #3764]
	.loc 3 661 0
	mov	r1, r5
	mov	r0, r10
	bl	__udivsi3
.LVL1602:
	.loc 3 662 0
	ubfx	r7, r0, #5, #16
	.loc 3 661 0
	mov	r8, r0
	.loc 3 662 0
	add	r3, r7, #36
	.loc 3 661 0
	str	r0, [r4, #2480]
	.loc 3 662 0
	strh	r3, [r4, r9]	@ movhi
	.loc 3 663 0
	mov	r3, #24
	mul	r3, r3, r5
	cmp	r6, r3
	ble	.L1752
	.loc 3 664 0
	mov	r1, r5
	sub	r0, r10, r6
	bl	__udivsi3
.LVL1603:
	str	r0, [r4, #2480]
	.loc 3 665 0
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r4, r9]	@ movhi
.L1752:
	.loc 3 668 0
	ldrh	r3, [r4, #70]
	cmp	r3, #0
	beq	.L1754
	.loc 3 669 0
	movw	r2, #2548
	ldrh	r1, [r4, r2]
	add	r1, r1, r3, lsr #1
	strh	r1, [r4, r2]	@ movhi
	.loc 3 670 0
	mul	r1, r5, r3
	cmp	r6, r1
	.loc 3 672 0
	addlt	r3, r3, #32
	.loc 3 671 0
	strlt	r8, [r4, #2480]
	.loc 3 672 0
	addlt	r3, r7, r3
	strhlt	r3, [r4, r2]	@ movhi
.L1754:
	.loc 3 677 0
	movw	r3, #2548
	ldr	r6, .L1764+8
.LVL1604:
	ldrh	r2, [r4, r3]
	.loc 3 689 0
	mvn	r7, #0
	.loc 3 677 0
	ldr	r3, [r4, #2480]
	sub	r3, r3, r2
	mul	r5, r5, r3
	.loc 3 678 0
	ldrh	r3, [r4, #78]
	.loc 3 677 0
	str	r5, [r6, #-4056]
	.loc 3 678 0
	mul	r5, r5, r3
	.loc 3 679 0
	ldrh	r3, [r4, #34]
	.loc 3 678 0
	str	r5, [r4, #2480]
	.loc 3 679 0
	mul	r5, r5, r3
	str	r5, [r4, #116]
	.loc 3 684 0
	bl	FtlBbmTblFlush
.LVL1605:
	.loc 3 687 0
	ldrh	r2, [r4, #18]
	mov	r1, #0
	ldr	r0, [r4, #2300]
	.loc 3 689 0
	ldr	r5, .L1764+12
	.loc 3 687 0
	lsl	r2, r2, #1
	.loc 3 694 0
	sub	r5, r5, #240
	.loc 3 687 0
	bl	ftl_memset
.LVL1606:
	.loc 3 689 0
	movw	r2, #2564
	.loc 3 688 0
	mov	r3, #0
	.loc 3 689 0
	strh	r7, [r4, r2]	@ movhi
	.loc 3 696 0
	movw	r2, #2324
	strh	r3, [r4, r2]	@ movhi
	.loc 3 698 0
	mov	r1, #255
	ldrh	r2, [r4, #16]
	.loc 3 690 0
	strh	r3, [r5, #242]	@ movhi
	.loc 3 701 0
	mov	r8, r5
	.loc 3 688 0
	str	r3, [r4, #2476]
	.loc 3 691 0
	strb	r3, [r4, #2570]
	.loc 3 692 0
	strb	r3, [r4, #2572]
	.loc 3 698 0
	lsr	r2, r2, #3
	.loc 3 694 0
	strh	r3, [r5, #2]	@ movhi
	.loc 3 695 0
	strb	r3, [r4, #2330]
	.loc 3 697 0
	mov	r3, #1
	.loc 3 698 0
	ldr	r0, [r4]
	.loc 3 697 0
	strb	r3, [r4, #2332]
	.loc 3 698 0
	bl	ftl_memset
.LVL1607:
.L1756:
	.loc 3 701 0
	mov	r0, r8
	bl	make_superblock
.LVL1608:
	.loc 3 702 0
	ldrb	r3, [r4, #2331]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1757
	.loc 3 705 0
	ldr	r2, [r4, #2300]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	.loc 3 706 0
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	.loc 3 707 0
	b	.L1756
.L1743:
	.loc 3 636 0 discriminator 3
	ldr	ip, [r4, #3776]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	.loc 3 637 0 discriminator 3
	ldr	r1, [r4, #3768]
	str	r0, [r1, r3, lsl #2]
	b	.L1742
.LVL1609:
.L1745:
	.loc 3 643 0 discriminator 3
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
.LVL1610:
	.loc 3 642 0 discriminator 3
	add	r6, r6, #1
	.loc 3 643 0 discriminator 3
	add	r5, r5, r0
.LVL1611:
	uxth	r5, r5
.LVL1612:
	.loc 3 642 0 discriminator 3
	uxth	r6, r6
	b	.L1744
.L1746:
	.loc 3 647 0
	mov	r0, r5
	bl	__udivsi3
.LVL1613:
	ldr	r3, [r4, #108]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
.LVL1614:
	.loc 3 648 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1615:
	.loc 3 650 0
	ldrh	r5, [r4, #16]
.LVL1616:
.L1748:
	.loc 3 650 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r5
	bls	.L1750
	.loc 3 651 0 is_stmt 1 discriminator 3
	mov	r0, r5
	mov	r1, #1
	.loc 3 650 0 discriminator 3
	add	r5, r5, #1
	.loc 3 651 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1617:
	.loc 3 650 0 discriminator 3
	uxth	r5, r5
	b	.L1748
.LVL1618:
.L1751:
	.loc 3 657 0 discriminator 3
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
.LVL1619:
	add	r6, r6, r0
.LVL1620:
	mov	r0, r5
	uxth	r6, r6
.LVL1621:
	b	.L1747
.LVL1622:
.L1757:
	.loc 3 709 0
	ldr	r2, [r4, #2516]
	.loc 3 710 0
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	.loc 3 721 0
	mvn	r7, #0
	.loc 3 709 0
	str	r2, [r4, #2336]
	add	r2, r2, #1
	str	r2, [r4, #2516]
	.loc 3 710 0
	ldr	r2, [r4, #2300]
	strh	r1, [r2, r3]	@ movhi
	.loc 3 712 0
	mov	r3, #0
	ldr	r2, .L1764+16
	.loc 3 714 0
	movw	r1, #2372
	.loc 3 713 0
	strb	r3, [r4, #2378]
	.loc 3 712 0
	strh	r3, [r2, #2]	@ movhi
	.loc 3 717 0
	mov	r8, r2
	.loc 3 714 0
	ldrh	r3, [r5]
	mov	r5, r2
	add	r3, r3, #1
	strh	r3, [r4, r1]	@ movhi
	.loc 3 715 0
	mov	r3, #1
	strb	r3, [r4, #2380]
.L1758:
	.loc 3 717 0
	mov	r0, r8
	bl	make_superblock
.LVL1623:
	.loc 3 718 0
	ldrb	r3, [r4, #2379]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1759
	.loc 3 721 0
	ldr	r2, [r4, #2300]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	.loc 3 722 0
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	.loc 3 723 0
	b	.L1758
.L1759:
	.loc 3 725 0
	ldr	r2, [r4, #2516]
	.loc 3 726 0
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	.loc 3 727 0
	mvn	r5, #0
	.loc 3 725 0
	str	r2, [r4, #2384]
	add	r2, r2, #1
	str	r2, [r4, #2516]
	.loc 3 726 0
	ldr	r2, [r4, #2300]
	strh	r1, [r2, r3]	@ movhi
	.loc 3 727 0
	movw	r3, #2420
	strh	r5, [r4, r3]	@ movhi
	.loc 3 730 0
	bl	FtlFreeSysBlkQueueOut
.LVL1624:
	ldr	r3, .L1764+20
	movw	r2, #2552
	strh	r0, [r4, r2]	@ movhi
	.loc 3 731 0
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	.loc 3 733 0
	ldr	r2, [r6, #-4056]
	.loc 3 732 0
	strh	r5, [r3, #4]	@ movhi
	.loc 3 733 0
	strh	r2, [r3, #6]	@ movhi
	.loc 3 734 0
	ldr	r3, [r4, #2516]
	str	r3, [r4, #2560]
	add	r3, r3, #1
	str	r3, [r4, #2516]
	.loc 3 735 0
	bl	FtlVpcTblFlush
.LVL1625:
	.loc 3 736 0
	bl	FtlSysBlkInit
.LVL1626:
	cmp	r0, #0
	.loc 3 739 0
	mov	r0, #0
	.loc 3 737 0
	ldreq	r3, .L1764+24
	moveq	r2, #1
	streq	r2, [r3]
	.loc 3 739 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1765:
	.align	2
.L1764:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR3
	.word	.LANCHOR0+2564
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+2552
	.word	.LANCHOR2
.LFE160:
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
.LFB161:
	.loc 3 743 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
.LCFI440:
	push	{r4, r5, fp, ip, lr, pc}
.LCFI441:
	sub	fp, ip, #4
.LCFI442:
	.loc 3 746 0
	ldr	r5, .L1772
	mvn	r3, #0
	.loc 3 753 0
	ldr	r4, .L1772+4
	.loc 3 749 0
	ldr	r1, .L1772+8
	ldr	r0, .L1772+12
	.loc 3 746 0
	str	r3, [r5]
	.loc 3 749 0
	bl	printk
.LVL1627:
	.loc 3 750 0
	ldr	r0, .L1772+16
	bl	FtlConstantsInit
.LVL1628:
	.loc 3 751 0
	bl	FtlMemInit
.LVL1629:
	.loc 3 752 0
	bl	FtlVariablesInit
.LVL1630:
	.loc 3 753 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1631:
	.loc 3 756 0
	bl	FtlLoadBbt
.LVL1632:
	cmp	r0, #0
	bne	.L1770
	.loc 3 767 0
	bl	FtlSysBlkInit
.LVL1633:
	cmp	r0, #0
	bne	.L1770
	.loc 3 779 0
	mov	r3, #1
	.loc 3 783 0
	add	r4, r4, #2320
	.loc 3 779 0
	str	r3, [r5]
	.loc 3 783 0
	ldrh	r3, [r4]
	cmp	r3, #15
	bhi	.L1770
	movw	r4, #8129
.L1769:
.LBB547:
	.loc 3 786 0 discriminator 3
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1634:
	.loc 3 785 0 discriminator 3
	subs	r4, r4, #1
	bne	.L1769
.L1770:
.LBE547:
	.loc 3 794 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1773:
	.align	2
.L1772:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC70
	.word	.LC71
	.word	g_nand_phy_info
.LFE161:
	.size	sftl_init, .-sftl_init
	.align	2
	.global	ftl_memcmp
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcmp, %function
ftl_memcmp:
.LFB279:
	.loc 1 211 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1635:
	mov	ip, sp
.LCFI443:
	push	{fp, ip, lr, pc}
.LCFI444:
	sub	fp, ip, #4
.LCFI445:
	.loc 1 212 0
	bl	memcmp
.LVL1636:
	.loc 1 213 0
	ldmfd	sp, {fp, sp, pc}
.LFE279:
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
	.type	__func__.3871, %object
	.size	__func__.3871, 17
__func__.3871:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.3866, %object
	.size	__func__.3866, 17
__func__.3866:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.3902, %object
	.size	__func__.3902, 17
__func__.3902:
	.ascii	"List_remove_node\000"
	.type	__func__.3934, %object
	.size	__func__.3934, 22
__func__.3934:
	.ascii	"List_update_data_list\000"
	.type	__func__.4040, %object
	.size	__func__.4040, 22
__func__.4040:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4363, %object
	.size	__func__.4363, 16
__func__.4363:
	.ascii	"make_superblock\000"
	.type	__func__.4538, %object
	.size	__func__.4538, 19
__func__.4538:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.4778, %object
	.size	__func__.4778, 15
__func__.4778:
	.ascii	"FlashReadPages\000"
	.type	__func__.4795, %object
	.size	__func__.4795, 15
__func__.4795:
	.ascii	"FlashProgPages\000"
	.type	__func__.3976, %object
	.size	__func__.3976, 26
__func__.3976:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3490, %object
	.size	__func__.3490, 11
__func__.3490:
	.ascii	"FtlMemInit\000"
	.type	__func__.3706, %object
	.size	__func__.3706, 14
__func__.3706:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.3943, %object
	.size	__func__.3943, 16
__func__.3943:
	.ascii	"load_l2p_region\000"
	.type	__func__.4001, %object
	.size	__func__.4001, 31
__func__.4001:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4015, %object
	.size	__func__.4015, 16
__func__.4015:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.3987, %object
	.size	__func__.3987, 15
__func__.3987:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.4057, %object
	.size	__func__.4057, 9
__func__.4057:
	.ascii	"log2phys\000"
	.type	__func__.4272, %object
	.size	__func__.4272, 16
__func__.4272:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4425, %object
	.size	__func__.4425, 14
__func__.4425:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.4152, %object
	.size	__func__.4152, 14
__func__.4152:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.4384, %object
	.size	__func__.4384, 18
__func__.4384:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.4510, %object
	.size	__func__.4510, 25
__func__.4510:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4551, %object
	.size	__func__.4551, 16
__func__.4551:
	.ascii	"update_vpc_list\000"
	.type	__func__.4558, %object
	.size	__func__.4558, 20
__func__.4558:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4306, %object
	.size	__func__.4306, 22
__func__.4306:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.3749, %object
	.size	__func__.3749, 11
__func__.3749:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.4130, %object
	.size	__func__.4130, 15
__func__.4130:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4409, %object
	.size	__func__.4409, 21
__func__.4409:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.4531, %object
	.size	__func__.4531, 29
__func__.4531:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3614, %object
	.size	__func__.3614, 13
__func__.3614:
	.ascii	"FtlProgPages\000"
	.type	__func__.4628, %object
	.size	__func__.4628, 19
__func__.4628:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4734, %object
	.size	__func__.4734, 23
__func__.4734:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.3642, %object
	.size	__func__.3642, 9
__func__.3642:
	.ascii	"FtlWrite\000"
	.type	__func__.4209, %object
	.size	__func__.4209, 15
__func__.4209:
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
	.set	.LANCHOR3,. + 8184
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
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI1-.LCFI0
	.byte	0x84
	.uleb128 0xa
	.byte	0x85
	.uleb128 0x9
	.byte	0x86
	.uleb128 0x8
	.byte	0x87
	.uleb128 0x7
	.byte	0x88
	.uleb128 0x6
	.byte	0x89
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI2-.LCFI1
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE0:
.LSFDE2:
	.4byte	.LEFDE2-.LASFDE2
.LASFDE2:
	.4byte	.Lframe0
	.4byte	.LFB286
	.4byte	.LFE286-.LFB286
	.byte	0x4
	.4byte	.LCFI3-.LFB286
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI4-.LCFI3
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI5-.LCFI4
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE2:
.LSFDE4:
	.4byte	.LEFDE4-.LASFDE4
.LASFDE4:
	.4byte	.Lframe0
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.byte	0x4
	.4byte	.LCFI6-.LFB152
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI7-.LCFI6
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI8-.LCFI7
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE4:
.LSFDE6:
	.4byte	.LEFDE6-.LASFDE6
.LASFDE6:
	.4byte	.Lframe0
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.byte	0x4
	.4byte	.LCFI9-.LFB153
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI10-.LCFI9
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI11-.LCFI10
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE6:
.LSFDE8:
	.4byte	.LEFDE8-.LASFDE8
.LASFDE8:
	.4byte	.Lframe0
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.byte	0x4
	.4byte	.LCFI12-.LFB154
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI13-.LCFI12
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI14-.LCFI13
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE8:
.LSFDE10:
	.4byte	.LEFDE10-.LASFDE10
.LASFDE10:
	.4byte	.Lframe0
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.byte	0x4
	.4byte	.LCFI15-.LFB155
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI16-.LCFI15
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI17-.LCFI16
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE10:
.LSFDE12:
	.4byte	.LEFDE12-.LASFDE12
.LASFDE12:
	.4byte	.Lframe0
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.byte	0x4
	.4byte	.LCFI18-.LFB158
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI19-.LCFI18
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI20-.LCFI19
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE12:
.LSFDE14:
	.4byte	.LEFDE14-.LASFDE14
.LASFDE14:
	.4byte	.Lframe0
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.byte	0x4
	.4byte	.LCFI21-.LFB171
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI22-.LCFI21
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI23-.LCFI22
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE14:
.LSFDE16:
	.4byte	.LEFDE16-.LASFDE16
.LASFDE16:
	.4byte	.Lframe0
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.byte	0x4
	.4byte	.LCFI24-.LFB172
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI25-.LCFI24
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI26-.LCFI25
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE16:
.LSFDE18:
	.4byte	.LEFDE18-.LASFDE18
.LASFDE18:
	.4byte	.Lframe0
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.byte	0x4
	.4byte	.LCFI27-.LFB173
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI28-.LCFI27
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI29-.LCFI28
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE18:
.LSFDE20:
	.4byte	.LEFDE20-.LASFDE20
.LASFDE20:
	.4byte	.Lframe0
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.byte	0x4
	.4byte	.LCFI30-.LFB174
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI31-.LCFI30
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI32-.LCFI31
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE20:
.LSFDE22:
	.4byte	.LEFDE22-.LASFDE22
.LASFDE22:
	.4byte	.Lframe0
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.byte	0x4
	.4byte	.LCFI33-.LFB177
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI34-.LCFI33
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI35-.LCFI34
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE22:
.LSFDE24:
	.4byte	.LEFDE24-.LASFDE24
.LASFDE24:
	.4byte	.Lframe0
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.byte	0x4
	.4byte	.LCFI36-.LFB178
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI37-.LCFI36
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI38-.LCFI37
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE24:
.LSFDE26:
	.4byte	.LEFDE26-.LASFDE26
.LASFDE26:
	.4byte	.Lframe0
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.byte	0x4
	.4byte	.LCFI39-.LFB182
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI40-.LCFI39
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI41-.LCFI40
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE26:
.LSFDE28:
	.4byte	.LEFDE28-.LASFDE28
.LASFDE28:
	.4byte	.Lframe0
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.byte	0x4
	.4byte	.LCFI42-.LFB183
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI43-.LCFI42
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI44-.LCFI43
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE28:
.LSFDE30:
	.4byte	.LEFDE30-.LASFDE30
.LASFDE30:
	.4byte	.Lframe0
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.byte	0x4
	.4byte	.LCFI45-.LFB184
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI46-.LCFI45
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI47-.LCFI46
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE30:
.LSFDE32:
	.4byte	.LEFDE32-.LASFDE32
.LASFDE32:
	.4byte	.Lframe0
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.byte	0x4
	.4byte	.LCFI48-.LFB185
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI49-.LCFI48
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI50-.LCFI49
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE32:
.LSFDE34:
	.4byte	.LEFDE34-.LASFDE34
.LASFDE34:
	.4byte	.Lframe0
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.byte	0x4
	.4byte	.LCFI51-.LFB187
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI52-.LCFI51
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI53-.LCFI52
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE34:
.LSFDE36:
	.4byte	.LEFDE36-.LASFDE36
.LASFDE36:
	.4byte	.Lframe0
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.byte	0x4
	.4byte	.LCFI54-.LFB188
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI55-.LCFI54
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI56-.LCFI55
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE36:
.LSFDE38:
	.4byte	.LEFDE38-.LASFDE38
.LASFDE38:
	.4byte	.Lframe0
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.byte	0x4
	.4byte	.LCFI57-.LFB189
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI58-.LCFI57
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI59-.LCFI58
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE38:
.LSFDE40:
	.4byte	.LEFDE40-.LASFDE40
.LASFDE40:
	.4byte	.Lframe0
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.byte	0x4
	.4byte	.LCFI60-.LFB191
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI61-.LCFI60
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI62-.LCFI61
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE40:
.LSFDE42:
	.4byte	.LEFDE42-.LASFDE42
.LASFDE42:
	.4byte	.Lframe0
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.byte	0x4
	.4byte	.LCFI63-.LFB192
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI64-.LCFI63
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI65-.LCFI64
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE42:
.LSFDE44:
	.4byte	.LEFDE44-.LASFDE44
.LASFDE44:
	.4byte	.Lframe0
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.byte	0x4
	.4byte	.LCFI66-.LFB196
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI67-.LCFI66
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI68-.LCFI67
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI69-.LCFI68
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI70-.LCFI69
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
	.4byte	.LCFI71-.LFB195
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI72-.LCFI71
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI73-.LCFI72
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE46:
.LSFDE48:
	.4byte	.LEFDE48-.LASFDE48
.LASFDE48:
	.4byte	.Lframe0
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.byte	0x4
	.4byte	.LCFI74-.LFB197
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI75-.LCFI74
	.byte	0x84
	.uleb128 0xa
	.byte	0x85
	.uleb128 0x9
	.byte	0x86
	.uleb128 0x8
	.byte	0x87
	.uleb128 0x7
	.byte	0x88
	.uleb128 0x6
	.byte	0x89
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI76-.LCFI75
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE48:
.LSFDE50:
	.4byte	.LEFDE50-.LASFDE50
.LASFDE50:
	.4byte	.Lframe0
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.byte	0x4
	.4byte	.LCFI77-.LFB194
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI78-.LCFI77
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI79-.LCFI78
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE50:
.LSFDE52:
	.4byte	.LEFDE52-.LASFDE52
.LASFDE52:
	.4byte	.Lframe0
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.byte	0x4
	.4byte	.LCFI80-.LFB198
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI81-.LCFI80
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI82-.LCFI81
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE52:
.LSFDE54:
	.4byte	.LEFDE54-.LASFDE54
.LASFDE54:
	.4byte	.Lframe0
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.byte	0x4
	.4byte	.LCFI83-.LFB199
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI84-.LCFI83
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI85-.LCFI84
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE54:
.LSFDE56:
	.4byte	.LEFDE56-.LASFDE56
.LASFDE56:
	.4byte	.Lframe0
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.byte	0x4
	.4byte	.LCFI86-.LFB200
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI87-.LCFI86
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI88-.LCFI87
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE56:
.LSFDE58:
	.4byte	.LEFDE58-.LASFDE58
.LASFDE58:
	.4byte	.Lframe0
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.byte	0x4
	.4byte	.LCFI89-.LFB201
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI90-.LCFI89
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI91-.LCFI90
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE58:
.LSFDE60:
	.4byte	.LEFDE60-.LASFDE60
.LASFDE60:
	.4byte	.Lframe0
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.byte	0x4
	.4byte	.LCFI92-.LFB202
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI93-.LCFI92
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI94-.LCFI93
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE60:
.LSFDE62:
	.4byte	.LEFDE62-.LASFDE62
.LASFDE62:
	.4byte	.Lframe0
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.byte	0x4
	.4byte	.LCFI95-.LFB210
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI96-.LCFI95
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI97-.LCFI96
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE62:
.LSFDE64:
	.4byte	.LEFDE64-.LASFDE64
.LASFDE64:
	.4byte	.Lframe0
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.byte	0x4
	.4byte	.LCFI98-.LFB216
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI99-.LCFI98
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI100-.LCFI99
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE64:
.LSFDE66:
	.4byte	.LEFDE66-.LASFDE66
.LASFDE66:
	.4byte	.Lframe0
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.byte	0x4
	.4byte	.LCFI101-.LFB222
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI102-.LCFI101
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI103-.LCFI102
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE66:
.LSFDE68:
	.4byte	.LEFDE68-.LASFDE68
.LASFDE68:
	.4byte	.Lframe0
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.byte	0x4
	.4byte	.LCFI104-.LFB223
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI105-.LCFI104
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI106-.LCFI105
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE68:
.LSFDE70:
	.4byte	.LEFDE70-.LASFDE70
.LASFDE70:
	.4byte	.Lframe0
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.byte	0x4
	.4byte	.LCFI107-.LFB229
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI108-.LCFI107
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI109-.LCFI108
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE70:
.LSFDE72:
	.4byte	.LEFDE72-.LASFDE72
.LASFDE72:
	.4byte	.Lframe0
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.byte	0x4
	.4byte	.LCFI110-.LFB232
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI111-.LCFI110
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI112-.LCFI111
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE72:
.LSFDE74:
	.4byte	.LEFDE74-.LASFDE74
.LASFDE74:
	.4byte	.Lframe0
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.byte	0x4
	.4byte	.LCFI113-.LFB234
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI114-.LCFI113
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI115-.LCFI114
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE74:
.LSFDE76:
	.4byte	.LEFDE76-.LASFDE76
.LASFDE76:
	.4byte	.Lframe0
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.byte	0x4
	.4byte	.LCFI116-.LFB243
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI117-.LCFI116
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI118-.LCFI117
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE76:
.LSFDE78:
	.4byte	.LEFDE78-.LASFDE78
.LASFDE78:
	.4byte	.Lframe0
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.byte	0x4
	.4byte	.LCFI119-.LFB244
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI120-.LCFI119
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI121-.LCFI120
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE78:
.LSFDE80:
	.4byte	.LEFDE80-.LASFDE80
.LASFDE80:
	.4byte	.Lframe0
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.byte	0x4
	.4byte	.LCFI122-.LFB245
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI123-.LCFI122
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI124-.LCFI123
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE80:
.LSFDE82:
	.4byte	.LEFDE82-.LASFDE82
.LASFDE82:
	.4byte	.Lframe0
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.byte	0x4
	.4byte	.LCFI125-.LFB150
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI126-.LCFI125
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI127-.LCFI126
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI128-.LCFI127
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI129-.LCFI128
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
	.4byte	.LCFI130-.LFB151
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI131-.LCFI130
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI132-.LCFI131
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE84:
.LSFDE86:
	.4byte	.LEFDE86-.LASFDE86
.LASFDE86:
	.4byte	.Lframe0
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.byte	0x4
	.4byte	.LCFI133-.LFB246
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI134-.LCFI133
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI135-.LCFI134
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI136-.LCFI135
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI137-.LCFI136
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
	.4byte	.LCFI138-.LFB247
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI139-.LCFI138
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI140-.LCFI139
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE88:
.LSFDE90:
	.4byte	.LEFDE90-.LASFDE90
.LASFDE90:
	.4byte	.Lframe0
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.byte	0x4
	.4byte	.LCFI141-.LFB250
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI142-.LCFI141
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI143-.LCFI142
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE90:
.LSFDE92:
	.4byte	.LEFDE92-.LASFDE92
.LASFDE92:
	.4byte	.Lframe0
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.byte	0x4
	.4byte	.LCFI144-.LFB253
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI145-.LCFI144
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI146-.LCFI145
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE92:
.LSFDE94:
	.4byte	.LEFDE94-.LASFDE94
.LASFDE94:
	.4byte	.Lframe0
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.byte	0x4
	.4byte	.LCFI147-.LFB254
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI148-.LCFI147
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI149-.LCFI148
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE94:
.LSFDE96:
	.4byte	.LEFDE96-.LASFDE96
.LASFDE96:
	.4byte	.Lframe0
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.byte	0x4
	.4byte	.LCFI150-.LFB255
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI151-.LCFI150
	.byte	0x84
	.uleb128 0xa
	.byte	0x85
	.uleb128 0x9
	.byte	0x86
	.uleb128 0x8
	.byte	0x87
	.uleb128 0x7
	.byte	0x88
	.uleb128 0x6
	.byte	0x89
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI152-.LCFI151
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE96:
.LSFDE98:
	.4byte	.LEFDE98-.LASFDE98
.LASFDE98:
	.4byte	.Lframe0
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.byte	0x4
	.4byte	.LCFI153-.LFB256
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI154-.LCFI153
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI155-.LCFI154
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE98:
.LSFDE100:
	.4byte	.LEFDE100-.LASFDE100
.LASFDE100:
	.4byte	.Lframe0
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.byte	0x4
	.4byte	.LCFI156-.LFB257
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI157-.LCFI156
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI158-.LCFI157
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE100:
.LSFDE102:
	.4byte	.LEFDE102-.LASFDE102
.LASFDE102:
	.4byte	.Lframe0
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.byte	0x4
	.4byte	.LCFI159-.LFB264
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI160-.LCFI159
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI161-.LCFI160
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE102:
.LSFDE104:
	.4byte	.LEFDE104-.LASFDE104
.LASFDE104:
	.4byte	.Lframe0
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.byte	0x4
	.4byte	.LCFI162-.LFB265
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI163-.LCFI162
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI164-.LCFI163
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE104:
.LSFDE106:
	.4byte	.LEFDE106-.LASFDE106
.LASFDE106:
	.4byte	.Lframe0
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.byte	0x4
	.4byte	.LCFI165-.LFB266
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI166-.LCFI165
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI167-.LCFI166
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE106:
.LSFDE108:
	.4byte	.LEFDE108-.LASFDE108
.LASFDE108:
	.4byte	.Lframe0
	.4byte	.LFB272
	.4byte	.LFE272-.LFB272
	.byte	0x4
	.4byte	.LCFI168-.LFB272
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI169-.LCFI168
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI170-.LCFI169
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI171-.LCFI170
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI172-.LCFI171
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
	.4byte	.LCFI173-.LFB180
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI174-.LCFI173
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI175-.LCFI174
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE110:
.LSFDE112:
	.4byte	.LEFDE112-.LASFDE112
.LASFDE112:
	.4byte	.Lframe0
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.byte	0x4
	.4byte	.LCFI176-.LFB186
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI177-.LCFI176
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI178-.LCFI177
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI179-.LCFI178
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI180-.LCFI179
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
	.4byte	.LCFI181-.LFB273
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI182-.LCFI181
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI183-.LCFI182
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI184-.LCFI183
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI185-.LCFI184
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
	.4byte	.LCFI186-.LFB274
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI187-.LCFI186
	.byte	0x84
	.uleb128 0xa
	.byte	0x85
	.uleb128 0x9
	.byte	0x86
	.uleb128 0x8
	.byte	0x87
	.uleb128 0x7
	.byte	0x88
	.uleb128 0x6
	.byte	0x89
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI188-.LCFI187
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI189-.LCFI188
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x28
	.byte	0x4
	.4byte	.LCFI190-.LCFI189
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
	.4byte	.LCFI191-.LFB190
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI192-.LCFI191
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI193-.LCFI192
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE118:
.LSFDE120:
	.4byte	.LEFDE120-.LASFDE120
.LASFDE120:
	.4byte	.Lframe0
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.byte	0x4
	.4byte	.LCFI194-.LFB159
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI195-.LCFI194
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI196-.LCFI195
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI197-.LCFI196
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI198-.LCFI197
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
	.4byte	.LCFI199-.LFB193
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI200-.LCFI199
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI201-.LCFI200
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE122:
.LSFDE124:
	.4byte	.LEFDE124-.LASFDE124
.LASFDE124:
	.4byte	.Lframe0
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.byte	0x4
	.4byte	.LCFI202-.LFB205
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI203-.LCFI202
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI204-.LCFI203
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE124:
.LSFDE126:
	.4byte	.LEFDE126-.LASFDE126
.LASFDE126:
	.4byte	.Lframe0
	.4byte	.LFB275
	.4byte	.LFE275-.LFB275
	.byte	0x4
	.4byte	.LCFI205-.LFB275
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI206-.LCFI205
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI207-.LCFI206
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI208-.LCFI207
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x18
	.align	2
.LEFDE126:
.LSFDE128:
	.4byte	.LEFDE128-.LASFDE128
.LASFDE128:
	.4byte	.Lframe0
	.4byte	.LFB276
	.4byte	.LFE276-.LFB276
	.byte	0x4
	.4byte	.LCFI209-.LFB276
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI210-.LCFI209
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI211-.LCFI210
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE128:
.LSFDE130:
	.4byte	.LEFDE130-.LASFDE130
.LASFDE130:
	.4byte	.Lframe0
	.4byte	.LFB277
	.4byte	.LFE277-.LFB277
	.byte	0x4
	.4byte	.LCFI212-.LFB277
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI213-.LCFI212
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI214-.LCFI213
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE130:
.LSFDE132:
	.4byte	.LEFDE132-.LASFDE132
.LASFDE132:
	.4byte	.Lframe0
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.byte	0x4
	.4byte	.LCFI215-.LFB156
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI216-.LCFI215
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI217-.LCFI216
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE132:
.LSFDE134:
	.4byte	.LEFDE134-.LASFDE134
.LASFDE134:
	.4byte	.Lframe0
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.byte	0x4
	.4byte	.LCFI218-.LFB175
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI219-.LCFI218
	.byte	0x84
	.uleb128 0xa
	.byte	0x85
	.uleb128 0x9
	.byte	0x86
	.uleb128 0x8
	.byte	0x87
	.uleb128 0x7
	.byte	0x88
	.uleb128 0x6
	.byte	0x89
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI220-.LCFI219
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE134:
.LSFDE136:
	.4byte	.LEFDE136-.LASFDE136
.LASFDE136:
	.4byte	.Lframe0
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.byte	0x4
	.4byte	.LCFI221-.LFB203
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI222-.LCFI221
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI223-.LCFI222
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE136:
.LSFDE138:
	.4byte	.LEFDE138-.LASFDE138
.LASFDE138:
	.4byte	.Lframe0
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.byte	0x4
	.4byte	.LCFI224-.LFB204
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI225-.LCFI224
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI226-.LCFI225
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI227-.LCFI226
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI228-.LCFI227
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
	.4byte	.LCFI229-.LFB207
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI230-.LCFI229
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI231-.LCFI230
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE140:
.LSFDE142:
	.4byte	.LEFDE142-.LASFDE142
.LASFDE142:
	.4byte	.Lframe0
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.byte	0x4
	.4byte	.LCFI232-.LFB208
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI233-.LCFI232
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI234-.LCFI233
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE142:
.LSFDE144:
	.4byte	.LEFDE144-.LASFDE144
.LASFDE144:
	.4byte	.Lframe0
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.byte	0x4
	.4byte	.LCFI235-.LFB206
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI236-.LCFI235
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI237-.LCFI236
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI238-.LCFI237
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI239-.LCFI238
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
	.4byte	.LCFI240-.LFB209
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI241-.LCFI240
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI242-.LCFI241
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE146:
.LSFDE148:
	.4byte	.LEFDE148-.LASFDE148
.LASFDE148:
	.4byte	.Lframe0
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.byte	0x4
	.4byte	.LCFI243-.LFB212
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI244-.LCFI243
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI245-.LCFI244
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE148:
.LSFDE150:
	.4byte	.LEFDE150-.LASFDE150
.LASFDE150:
	.4byte	.Lframe0
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.byte	0x4
	.4byte	.LCFI246-.LFB230
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI247-.LCFI246
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI248-.LCFI247
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE150:
.LSFDE152:
	.4byte	.LEFDE152-.LASFDE152
.LASFDE152:
	.4byte	.Lframe0
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.byte	0x4
	.4byte	.LCFI249-.LFB241
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI250-.LCFI249
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI251-.LCFI250
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI252-.LCFI251
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x20
	.byte	0x4
	.4byte	.LCFI253-.LCFI252
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
	.4byte	.LCFI254-.LFB268
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI255-.LCFI254
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI256-.LCFI255
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI257-.LCFI256
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI258-.LCFI257
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
	.4byte	.LCFI259-.LFB240
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI260-.LCFI259
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI261-.LCFI260
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE156:
.LSFDE158:
	.4byte	.LEFDE158-.LASFDE158
.LASFDE158:
	.4byte	.Lframe0
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.byte	0x4
	.4byte	.LCFI262-.LFB217
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI263-.LCFI262
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI264-.LCFI263
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE158:
.LSFDE160:
	.4byte	.LEFDE160-.LASFDE160
.LASFDE160:
	.4byte	.Lframe0
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.byte	0x4
	.4byte	.LCFI265-.LFB220
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI266-.LCFI265
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI267-.LCFI266
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI268-.LCFI267
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI269-.LCFI268
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
	.4byte	.LCFI270-.LFB225
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI271-.LCFI270
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI272-.LCFI271
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI273-.LCFI272
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI274-.LCFI273
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
	.4byte	.LCFI275-.LFB226
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI276-.LCFI275
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI277-.LCFI276
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE164:
.LSFDE166:
	.4byte	.LEFDE166-.LASFDE166
.LASFDE166:
	.4byte	.Lframe0
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.byte	0x4
	.4byte	.LCFI278-.LFB227
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI279-.LCFI278
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI280-.LCFI279
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE166:
.LSFDE168:
	.4byte	.LEFDE168-.LASFDE168
.LASFDE168:
	.4byte	.Lframe0
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.byte	0x4
	.4byte	.LCFI281-.LFB228
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI282-.LCFI281
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI283-.LCFI282
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE168:
.LSFDE170:
	.4byte	.LEFDE170-.LASFDE170
.LASFDE170:
	.4byte	.Lframe0
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.byte	0x4
	.4byte	.LCFI284-.LFB157
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI285-.LCFI284
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI286-.LCFI285
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE170:
.LSFDE172:
	.4byte	.LEFDE172-.LASFDE172
.LASFDE172:
	.4byte	.Lframe0
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.byte	0x4
	.4byte	.LCFI287-.LFB235
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI288-.LCFI287
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI289-.LCFI288
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI290-.LCFI289
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE172:
.LSFDE174:
	.4byte	.LEFDE174-.LASFDE174
.LASFDE174:
	.4byte	.Lframe0
	.4byte	.LFB259
	.4byte	.LFE259-.LFB259
	.byte	0x4
	.4byte	.LCFI291-.LFB259
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI292-.LCFI291
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI293-.LCFI292
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE174:
.LSFDE176:
	.4byte	.LEFDE176-.LASFDE176
.LASFDE176:
	.4byte	.Lframe0
	.4byte	.LFB260
	.4byte	.LFE260-.LFB260
	.byte	0x4
	.4byte	.LCFI294-.LFB260
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI295-.LCFI294
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI296-.LCFI295
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI297-.LCFI296
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE176:
.LSFDE178:
	.4byte	.LEFDE178-.LASFDE178
.LASFDE178:
	.4byte	.Lframe0
	.4byte	.LFB278
	.4byte	.LFE278-.LFB278
	.byte	0x4
	.4byte	.LCFI298-.LFB278
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI299-.LCFI298
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI300-.LCFI299
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE178:
.LSFDE180:
	.4byte	.LEFDE180-.LASFDE180
.LASFDE180:
	.4byte	.Lframe0
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.byte	0x4
	.4byte	.LCFI301-.LFB176
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI302-.LCFI301
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI303-.LCFI302
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE180:
.LSFDE182:
	.4byte	.LEFDE182-.LASFDE182
.LASFDE182:
	.4byte	.Lframe0
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.byte	0x4
	.4byte	.LCFI304-.LFB248
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI305-.LCFI304
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI306-.LCFI305
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI307-.LCFI306
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE182:
.LSFDE184:
	.4byte	.LEFDE184-.LASFDE184
.LASFDE184:
	.4byte	.Lframe0
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.byte	0x4
	.4byte	.LCFI308-.LFB267
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI309-.LCFI308
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI310-.LCFI309
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI311-.LCFI310
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI312-.LCFI311
	.byte	0xb
	.align	2
.LEFDE184:
.LSFDE186:
	.4byte	.LEFDE186-.LASFDE186
.LASFDE186:
	.4byte	.Lframe0
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.byte	0x4
	.4byte	.LCFI313-.LFB251
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI314-.LCFI313
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI315-.LCFI314
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE186:
.LSFDE188:
	.4byte	.LEFDE188-.LASFDE188
.LASFDE188:
	.4byte	.Lframe0
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.byte	0x4
	.4byte	.LCFI316-.LFB252
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI317-.LCFI316
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI318-.LCFI317
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE188:
.LSFDE190:
	.4byte	.LEFDE190-.LASFDE190
.LASFDE190:
	.4byte	.Lframe0
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.byte	0x4
	.4byte	.LCFI319-.LFB218
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI320-.LCFI319
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI321-.LCFI320
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI322-.LCFI321
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI323-.LCFI322
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
	.4byte	.LCFI324-.LFB211
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI325-.LCFI324
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI326-.LCFI325
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE192:
.LSFDE194:
	.4byte	.LEFDE194-.LASFDE194
.LASFDE194:
	.4byte	.Lframe0
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.byte	0x4
	.4byte	.LCFI327-.LFB231
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI328-.LCFI327
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI329-.LCFI328
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI330-.LCFI329
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI331-.LCFI330
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
	.4byte	.LCFI332-.LFB238
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI333-.LCFI332
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI334-.LCFI333
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI335-.LCFI334
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI336-.LCFI335
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
	.4byte	.LCFI337-.LFB179
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI338-.LCFI337
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI339-.LCFI338
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE198:
.LSFDE200:
	.4byte	.LEFDE200-.LASFDE200
.LASFDE200:
	.4byte	.Lframe0
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.byte	0x4
	.4byte	.LCFI340-.LFB181
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI341-.LCFI340
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI342-.LCFI341
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE200:
.LSFDE202:
	.4byte	.LEFDE202-.LASFDE202
.LASFDE202:
	.4byte	.Lframe0
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.byte	0x4
	.4byte	.LCFI343-.LFB213
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI344-.LCFI343
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI345-.LCFI344
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI346-.LCFI345
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI347-.LCFI346
	.byte	0xb
	.align	2
.LEFDE202:
.LSFDE204:
	.4byte	.LEFDE204-.LASFDE204
.LASFDE204:
	.4byte	.Lframe0
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.byte	0x4
	.4byte	.LCFI348-.LFB236
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI349-.LCFI348
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI350-.LCFI349
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE204:
.LSFDE206:
	.4byte	.LEFDE206-.LASFDE206
.LASFDE206:
	.4byte	.Lframe0
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.byte	0x4
	.4byte	.LCFI351-.LFB215
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI352-.LCFI351
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI353-.LCFI352
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE206:
.LSFDE208:
	.4byte	.LEFDE208-.LASFDE208
.LASFDE208:
	.4byte	.Lframe0
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.byte	0x4
	.4byte	.LCFI354-.LFB214
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI355-.LCFI354
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI356-.LCFI355
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI357-.LCFI356
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI358-.LCFI357
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
	.4byte	.LCFI359-.LFB221
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI360-.LCFI359
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI361-.LCFI360
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE210:
.LSFDE212:
	.4byte	.LEFDE212-.LASFDE212
.LASFDE212:
	.4byte	.Lframe0
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.byte	0x4
	.4byte	.LCFI362-.LFB237
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI363-.LCFI362
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI364-.LCFI363
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE212:
.LSFDE214:
	.4byte	.LEFDE214-.LASFDE214
.LASFDE214:
	.4byte	.Lframe0
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.byte	0x4
	.4byte	.LCFI365-.LFB219
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI366-.LCFI365
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI367-.LCFI366
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE214:
.LSFDE216:
	.4byte	.LEFDE216-.LASFDE216
.LASFDE216:
	.4byte	.Lframe0
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.byte	0x4
	.4byte	.LCFI368-.LFB239
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI369-.LCFI368
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI370-.LCFI369
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE216:
.LSFDE218:
	.4byte	.LEFDE218-.LASFDE218
.LASFDE218:
	.4byte	.Lframe0
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.byte	0x4
	.4byte	.LCFI371-.LFB249
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI372-.LCFI371
	.byte	0x84
	.uleb128 0x8
	.byte	0x85
	.uleb128 0x7
	.byte	0x86
	.uleb128 0x6
	.byte	0x87
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI373-.LCFI372
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE218:
.LSFDE220:
	.4byte	.LEFDE220-.LASFDE220
.LASFDE220:
	.4byte	.Lframe0
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.byte	0x4
	.4byte	.LCFI374-.LFB165
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI375-.LCFI374
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI376-.LCFI375
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI377-.LCFI376
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE220:
.LSFDE222:
	.4byte	.LEFDE222-.LASFDE222
.LASFDE222:
	.4byte	.Lframe0
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.byte	0x4
	.4byte	.LCFI378-.LFB166
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI379-.LCFI378
	.byte	0x84
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI380-.LCFI379
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE222:
.LSFDE224:
	.4byte	.LEFDE224-.LASFDE224
.LASFDE224:
	.4byte	.Lframe0
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.byte	0x4
	.4byte	.LCFI381-.LFB162
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI382-.LCFI381
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI383-.LCFI382
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE224:
.LSFDE226:
	.4byte	.LEFDE226-.LASFDE226
.LASFDE226:
	.4byte	.Lframe0
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.byte	0x4
	.4byte	.LCFI384-.LFB163
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI385-.LCFI384
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI386-.LCFI385
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE226:
.LSFDE228:
	.4byte	.LEFDE228-.LASFDE228
.LASFDE228:
	.4byte	.Lframe0
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.byte	0x4
	.4byte	.LCFI387-.LFB168
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI388-.LCFI387
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI389-.LCFI388
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI390-.LCFI389
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI391-.LCFI390
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
	.4byte	.LCFI392-.LFB258
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI393-.LCFI392
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI394-.LCFI393
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE230:
.LSFDE232:
	.4byte	.LEFDE232-.LASFDE232
.LASFDE232:
	.4byte	.Lframe0
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.byte	0x4
	.4byte	.LCFI395-.LFB261
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI396-.LCFI395
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI397-.LCFI396
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE232:
.LSFDE234:
	.4byte	.LEFDE234-.LASFDE234
.LASFDE234:
	.4byte	.Lframe0
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.byte	0x4
	.4byte	.LCFI398-.LFB233
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI399-.LCFI398
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI400-.LCFI399
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE234:
.LSFDE236:
	.4byte	.LEFDE236-.LASFDE236
.LASFDE236:
	.4byte	.Lframe0
	.4byte	.LFB263
	.4byte	.LFE263-.LFB263
	.byte	0x4
	.4byte	.LCFI401-.LFB263
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI402-.LCFI401
	.byte	0x84
	.uleb128 0x7
	.byte	0x85
	.uleb128 0x6
	.byte	0x86
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI403-.LCFI402
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE236:
.LSFDE238:
	.4byte	.LEFDE238-.LASFDE238
.LASFDE238:
	.4byte	.Lframe0
	.4byte	.LFB262
	.4byte	.LFE262-.LFB262
	.byte	0x4
	.4byte	.LCFI404-.LFB262
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI405-.LCFI404
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI406-.LCFI405
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE238:
.LSFDE240:
	.4byte	.LEFDE240-.LASFDE240
.LASFDE240:
	.4byte	.Lframe0
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.byte	0x4
	.4byte	.LCFI407-.LFB269
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI408-.LCFI407
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI409-.LCFI408
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI410-.LCFI409
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI411-.LCFI410
	.byte	0xb
	.align	2
.LEFDE240:
.LSFDE242:
	.4byte	.LEFDE242-.LASFDE242
.LASFDE242:
	.4byte	.Lframe0
	.4byte	.LFB270
	.4byte	.LFE270-.LFB270
	.byte	0x4
	.4byte	.LCFI412-.LFB270
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI413-.LCFI412
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI414-.LCFI413
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE242:
.LSFDE244:
	.4byte	.LEFDE244-.LASFDE244
.LASFDE244:
	.4byte	.Lframe0
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.byte	0x4
	.4byte	.LCFI415-.LFB164
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI416-.LCFI415
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI417-.LCFI416
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI418-.LCFI417
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI419-.LCFI418
	.byte	0xb
	.align	2
.LEFDE244:
.LSFDE246:
	.4byte	.LEFDE246-.LASFDE246
.LASFDE246:
	.4byte	.Lframe0
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.byte	0x4
	.4byte	.LCFI420-.LFB169
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI421-.LCFI420
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI422-.LCFI421
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE246:
.LSFDE248:
	.4byte	.LEFDE248-.LASFDE248
.LASFDE248:
	.4byte	.Lframe0
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.byte	0x4
	.4byte	.LCFI423-.LFB167
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI424-.LCFI423
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI425-.LCFI424
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI426-.LCFI425
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI427-.LCFI426
	.byte	0xb
	.align	2
.LEFDE248:
.LSFDE250:
	.4byte	.LEFDE250-.LASFDE250
.LASFDE250:
	.4byte	.Lframe0
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.byte	0x4
	.4byte	.LCFI428-.LFB170
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI429-.LCFI428
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI430-.LCFI429
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE250:
.LSFDE252:
	.4byte	.LEFDE252-.LASFDE252
.LASFDE252:
	.4byte	.Lframe0
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.byte	0x4
	.4byte	.LCFI431-.LFB224
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI432-.LCFI431
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI433-.LCFI432
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE252:
.LSFDE254:
	.4byte	.LEFDE254-.LASFDE254
.LASFDE254:
	.4byte	.Lframe0
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.byte	0x4
	.4byte	.LCFI434-.LFB242
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI435-.LCFI434
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
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI436-.LCFI435
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE254:
.LSFDE256:
	.4byte	.LEFDE256-.LASFDE256
.LASFDE256:
	.4byte	.Lframe0
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.byte	0x4
	.4byte	.LCFI437-.LFB160
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI438-.LCFI437
	.byte	0x84
	.uleb128 0xb
	.byte	0x85
	.uleb128 0xa
	.byte	0x86
	.uleb128 0x9
	.byte	0x87
	.uleb128 0x8
	.byte	0x88
	.uleb128 0x7
	.byte	0x89
	.uleb128 0x6
	.byte	0x8a
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI439-.LCFI438
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE256:
.LSFDE258:
	.4byte	.LEFDE258-.LASFDE258
.LASFDE258:
	.4byte	.Lframe0
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.byte	0x4
	.4byte	.LCFI440-.LFB161
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI441-.LCFI440
	.byte	0x84
	.uleb128 0x6
	.byte	0x85
	.uleb128 0x5
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI442-.LCFI441
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE258:
.LSFDE260:
	.4byte	.LEFDE260-.LASFDE260
.LASFDE260:
	.4byte	.Lframe0
	.4byte	.LFB279
	.4byte	.LFE279-.LFB279
	.byte	0x4
	.4byte	.LCFI443-.LFB279
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI444-.LCFI443
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI445-.LCFI444
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
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
	.4byte	0x6832
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
	.byte	0x7
	.4byte	.LASF0
	.uleb128 0x2
	.byte	0x4
	.byte	0x7
	.4byte	.LASF1
	.uleb128 0x2
	.byte	0x1
	.byte	0x8
	.4byte	.LASF2
	.uleb128 0x3
	.4byte	0x33
	.uleb128 0x2
	.byte	0x4
	.byte	0x7
	.4byte	.LASF3
	.uleb128 0x2
	.byte	0x1
	.byte	0x6
	.4byte	.LASF4
	.uleb128 0x2
	.byte	0x1
	.byte	0x8
	.4byte	.LASF5
	.uleb128 0x2
	.byte	0x2
	.byte	0x5
	.4byte	.LASF6
	.uleb128 0x2
	.byte	0x2
	.byte	0x7
	.4byte	.LASF7
	.uleb128 0x4
	.byte	0x4
	.byte	0x5
	.ascii	"int\000"
	.uleb128 0x2
	.byte	0x8
	.byte	0x5
	.4byte	.LASF8
	.uleb128 0x2
	.byte	0x8
	.byte	0x7
	.4byte	.LASF9
	.uleb128 0x5
	.ascii	"u8\000"
	.byte	0x6
	.byte	0x10
	.4byte	0x4d
	.uleb128 0x5
	.ascii	"u16\000"
	.byte	0x6
	.byte	0x13
	.4byte	0x5b
	.uleb128 0x5
	.ascii	"s32\000"
	.byte	0x6
	.byte	0x15
	.4byte	0x62
	.uleb128 0x5
	.ascii	"u32\000"
	.byte	0x6
	.byte	0x16
	.4byte	0x3f
	.uleb128 0x2
	.byte	0x4
	.byte	0x5
	.4byte	.LASF10
	.uleb128 0x6
	.4byte	.LASF11
	.byte	0x7
	.byte	0x43
	.4byte	0x3f
	.uleb128 0x7
	.byte	0x4
	.4byte	0x33
	.uleb128 0x6
	.4byte	.LASF12
	.byte	0x8
	.byte	0x1d
	.4byte	0xc5
	.uleb128 0x2
	.byte	0x1
	.byte	0x2
	.4byte	.LASF13
	.uleb128 0x6
	.4byte	.LASF14
	.byte	0x8
	.byte	0x36
	.4byte	0xa9
	.uleb128 0x8
	.4byte	.LASF15
	.byte	0x9
	.byte	0xd
	.4byte	0x3f
	.uleb128 0x8
	.4byte	.LASF16
	.byte	0x9
	.byte	0xd
	.4byte	0x3f
	.uleb128 0x6
	.4byte	.LASF17
	.byte	0xa
	.byte	0x83
	.4byte	0xf8
	.uleb128 0x7
	.byte	0x4
	.4byte	0xfe
	.uleb128 0x9
	.4byte	0x62
	.uleb128 0x7
	.byte	0x4
	.4byte	0x109
	.uleb128 0xa
	.uleb128 0xb
	.4byte	0xed
	.4byte	0x115
	.uleb128 0xc
	.byte	0
	.uleb128 0x8
	.4byte	.LASF18
	.byte	0xa
	.byte	0x86
	.4byte	0x10a
	.uleb128 0x8
	.4byte	.LASF19
	.byte	0xa
	.byte	0x86
	.4byte	0x10a
	.uleb128 0x8
	.4byte	.LASF20
	.byte	0xa
	.byte	0x87
	.4byte	0x10a
	.uleb128 0x8
	.4byte	.LASF21
	.byte	0xa
	.byte	0x87
	.4byte	0x10a
	.uleb128 0xb
	.4byte	0x33
	.4byte	0x14c
	.uleb128 0xc
	.byte	0
	.uleb128 0x8
	.4byte	.LASF22
	.byte	0xa
	.byte	0x8e
	.4byte	0x141
	.uleb128 0x8
	.4byte	.LASF23
	.byte	0xa
	.byte	0x8f
	.4byte	0xb4
	.uleb128 0x8
	.4byte	.LASF24
	.byte	0xa
	.byte	0x90
	.4byte	0x3f
	.uleb128 0x8
	.4byte	.LASF25
	.byte	0xa
	.byte	0x9c
	.4byte	0x103
	.uleb128 0x8
	.4byte	.LASF26
	.byte	0xa
	.byte	0x9e
	.4byte	0xba
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x18e
	.uleb128 0xc
	.byte	0
	.uleb128 0x3
	.4byte	0x183
	.uleb128 0x8
	.4byte	.LASF27
	.byte	0xb
	.byte	0xa
	.4byte	0x18e
	.uleb128 0x8
	.4byte	.LASF28
	.byte	0xb
	.byte	0xb
	.4byte	0x18e
	.uleb128 0xb
	.4byte	0x62
	.4byte	0x1b4
	.uleb128 0xc
	.byte	0
	.uleb128 0x8
	.4byte	.LASF29
	.byte	0xb
	.byte	0x2e
	.4byte	0x1a9
	.uleb128 0x8
	.4byte	.LASF30
	.byte	0xb
	.byte	0x9f
	.4byte	0x62
	.uleb128 0x8
	.4byte	.LASF31
	.byte	0xb
	.byte	0xa0
	.4byte	0x62
	.uleb128 0x8
	.4byte	.LASF32
	.byte	0xb
	.byte	0xa1
	.4byte	0x62
	.uleb128 0xd
	.4byte	.LASF34
	.uleb128 0x3
	.4byte	0x1e0
	.uleb128 0xe
	.4byte	.LASF33
	.byte	0xb
	.2byte	0x1a4
	.4byte	0x1e5
	.uleb128 0xd
	.4byte	.LASF35
	.uleb128 0xe
	.4byte	.LASF36
	.byte	0xc
	.2byte	0x104
	.4byte	0x1f6
	.uleb128 0xf
	.4byte	0xa2
	.4byte	0x216
	.uleb128 0x10
	.4byte	0x62
	.byte	0
	.uleb128 0xe
	.4byte	.LASF37
	.byte	0xc
	.2byte	0x105
	.4byte	0x222
	.uleb128 0x7
	.byte	0x4
	.4byte	0x207
	.uleb128 0xe
	.4byte	.LASF38
	.byte	0xc
	.2byte	0x1bf
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF39
	.byte	0xc
	.2byte	0x1c0
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF40
	.byte	0xc
	.2byte	0x1c1
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF41
	.byte	0xc
	.2byte	0x1c2
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF42
	.byte	0xc
	.2byte	0x1c3
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF43
	.byte	0xc
	.2byte	0x1c4
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF44
	.byte	0xc
	.2byte	0x1c5
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF45
	.byte	0xc
	.2byte	0x1c7
	.4byte	0xba
	.uleb128 0xe
	.4byte	.LASF46
	.byte	0xc
	.2byte	0x1da
	.4byte	0x62
	.uleb128 0xe
	.4byte	.LASF47
	.byte	0xc
	.2byte	0x1dc
	.4byte	0xba
	.uleb128 0x11
	.4byte	.LASF666
	.byte	0x4
	.4byte	0x3f
	.byte	0xc
	.2byte	0x1df
	.4byte	0x2d0
	.uleb128 0x12
	.4byte	.LASF48
	.byte	0
	.uleb128 0x12
	.4byte	.LASF49
	.byte	0x1
	.uleb128 0x12
	.4byte	.LASF50
	.byte	0x2
	.uleb128 0x12
	.4byte	.LASF51
	.byte	0x3
	.uleb128 0x12
	.4byte	.LASF52
	.byte	0x4
	.byte	0
	.uleb128 0xe
	.4byte	.LASF53
	.byte	0xc
	.2byte	0x1e5
	.4byte	0x2a0
	.uleb128 0xe
	.4byte	.LASF54
	.byte	0xc
	.2byte	0x1f8
	.4byte	0x18e
	.uleb128 0xe
	.4byte	.LASF55
	.byte	0xc
	.2byte	0x203
	.4byte	0x18e
	.uleb128 0x6
	.4byte	.LASF56
	.byte	0xd
	.byte	0x9
	.4byte	0x4d
	.uleb128 0x6
	.4byte	.LASF57
	.byte	0xd
	.byte	0xa
	.4byte	0x5b
	.uleb128 0x6
	.4byte	.LASF58
	.byte	0xd
	.byte	0xb
	.4byte	0x3f
	.uleb128 0x6
	.4byte	.LASF59
	.byte	0xd
	.byte	0xc
	.4byte	0x62
	.uleb128 0x6
	.4byte	.LASF60
	.byte	0xd
	.byte	0xd
	.4byte	0x54
	.uleb128 0x13
	.4byte	.LASF65
	.byte	0x14
	.byte	0xe
	.byte	0x22
	.4byte	0x374
	.uleb128 0x14
	.4byte	.LASF61
	.byte	0xe
	.byte	0x23
	.4byte	0x97
	.byte	0
	.uleb128 0x14
	.4byte	.LASF62
	.byte	0xe
	.byte	0x24
	.4byte	0x97
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF63
	.byte	0xe
	.byte	0x25
	.4byte	0x374
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF64
	.byte	0xe
	.byte	0x26
	.4byte	0x374
	.byte	0xc
	.uleb128 0x15
	.ascii	"lpa\000"
	.byte	0xe
	.byte	0x27
	.4byte	0x97
	.byte	0x10
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x97
	.uleb128 0x13
	.4byte	.LASF66
	.byte	0x18
	.byte	0xe
	.byte	0x2a
	.4byte	0x417
	.uleb128 0x14
	.4byte	.LASF67
	.byte	0xe
	.byte	0x2b
	.4byte	0x81
	.byte	0
	.uleb128 0x14
	.4byte	.LASF68
	.byte	0xe
	.byte	0x2c
	.4byte	0x81
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF69
	.byte	0xe
	.byte	0x2d
	.4byte	0x81
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF70
	.byte	0xe
	.byte	0x2e
	.4byte	0x81
	.byte	0x6
	.uleb128 0x14
	.4byte	.LASF71
	.byte	0xe
	.byte	0x2f
	.4byte	0x81
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF72
	.byte	0xe
	.byte	0x30
	.4byte	0x81
	.byte	0xa
	.uleb128 0x14
	.4byte	.LASF73
	.byte	0xe
	.byte	0x31
	.4byte	0x81
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF74
	.byte	0xe
	.byte	0x32
	.4byte	0x81
	.byte	0xe
	.uleb128 0x14
	.4byte	.LASF75
	.byte	0xe
	.byte	0x33
	.4byte	0x81
	.byte	0x10
	.uleb128 0x14
	.4byte	.LASF76
	.byte	0xe
	.byte	0x34
	.4byte	0x81
	.byte	0x12
	.uleb128 0x14
	.4byte	.LASF77
	.byte	0xe
	.byte	0x35
	.4byte	0x81
	.byte	0x14
	.uleb128 0x14
	.4byte	.LASF78
	.byte	0xe
	.byte	0x36
	.4byte	0x77
	.byte	0x16
	.byte	0
	.uleb128 0x3
	.4byte	0x37a
	.uleb128 0x13
	.4byte	.LASF79
	.byte	0x10
	.byte	0xe
	.byte	0x39
	.4byte	0x459
	.uleb128 0x14
	.4byte	.LASF80
	.byte	0xe
	.byte	0x3a
	.4byte	0x473
	.byte	0
	.uleb128 0x14
	.4byte	.LASF81
	.byte	0xe
	.byte	0x3b
	.4byte	0x48d
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF82
	.byte	0xe
	.byte	0x3c
	.4byte	0x4b1
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF83
	.byte	0xe
	.byte	0x3d
	.4byte	0x4b1
	.byte	0xc
	.byte	0
	.uleb128 0xf
	.4byte	0x8c
	.4byte	0x46d
	.uleb128 0x10
	.4byte	0x46d
	.uleb128 0x10
	.4byte	0x97
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x81
	.uleb128 0x7
	.byte	0x4
	.4byte	0x459
	.uleb128 0xf
	.4byte	0x97
	.4byte	0x48d
	.uleb128 0x10
	.4byte	0x77
	.uleb128 0x10
	.4byte	0x97
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x479
	.uleb128 0xf
	.4byte	0x97
	.4byte	0x4b1
	.uleb128 0x10
	.4byte	0x77
	.uleb128 0x10
	.4byte	0x97
	.uleb128 0x10
	.4byte	0x374
	.uleb128 0x10
	.4byte	0x374
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x493
	.uleb128 0x13
	.4byte	.LASF84
	.byte	0x3c
	.byte	0xf
	.byte	0x2b
	.4byte	0x517
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0x2d
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF85
	.byte	0xf
	.byte	0x2e
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF86
	.byte	0xf
	.byte	0x2f
	.4byte	0x2ff
	.byte	0x4
	.uleb128 0x15
	.ascii	"cnt\000"
	.byte	0xf
	.byte	0x30
	.4byte	0x2ff
	.byte	0x6
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x31
	.4byte	0x30a
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF88
	.byte	0xf
	.byte	0x32
	.4byte	0x517
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF89
	.byte	0xf
	.byte	0x33
	.4byte	0x527
	.byte	0x1c
	.byte	0
	.uleb128 0xb
	.4byte	0x2ff
	.4byte	0x527
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x7
	.byte	0
	.uleb128 0xb
	.4byte	0x537
	.4byte	0x537
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x7
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x30a
	.uleb128 0x13
	.4byte	.LASF90
	.byte	0xc
	.byte	0xf
	.byte	0x3f
	.4byte	0x585
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0x41
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF85
	.byte	0xf
	.byte	0x42
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF86
	.byte	0xf
	.byte	0x43
	.4byte	0x2ff
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF91
	.byte	0xf
	.byte	0x44
	.4byte	0x2ff
	.byte	0x6
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x45
	.4byte	0x30a
	.byte	0x8
	.byte	0
	.uleb128 0x13
	.4byte	.LASF92
	.byte	0x28
	.byte	0xf
	.byte	0x48
	.4byte	0x62e
	.uleb128 0x14
	.4byte	.LASF93
	.byte	0xf
	.byte	0x4a
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF85
	.byte	0xf
	.byte	0x4b
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF94
	.byte	0xf
	.byte	0x4d
	.4byte	0x2ff
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF95
	.byte	0xf
	.byte	0x4e
	.4byte	0x2ff
	.byte	0x6
	.uleb128 0x14
	.4byte	.LASF96
	.byte	0xf
	.byte	0x50
	.4byte	0x2ff
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF97
	.byte	0xf
	.byte	0x51
	.4byte	0x2ff
	.byte	0xa
	.uleb128 0x14
	.4byte	.LASF98
	.byte	0xf
	.byte	0x53
	.4byte	0x62e
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF99
	.byte	0xf
	.byte	0x54
	.4byte	0x537
	.byte	0x10
	.uleb128 0x14
	.4byte	.LASF100
	.byte	0xf
	.byte	0x55
	.4byte	0x62e
	.byte	0x14
	.uleb128 0x14
	.4byte	.LASF101
	.byte	0xf
	.byte	0x56
	.4byte	0x537
	.byte	0x18
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x58
	.4byte	0x30a
	.byte	0x1c
	.uleb128 0x14
	.4byte	.LASF102
	.byte	0xf
	.byte	0x59
	.4byte	0x30a
	.byte	0x20
	.uleb128 0x14
	.4byte	.LASF103
	.byte	0xf
	.byte	0x5a
	.4byte	0x30a
	.byte	0x24
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x2ff
	.uleb128 0x13
	.4byte	.LASF104
	.byte	0xc
	.byte	0xf
	.byte	0x5d
	.4byte	0x671
	.uleb128 0x14
	.4byte	.LASF105
	.byte	0xf
	.byte	0x5f
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF106
	.byte	0xf
	.byte	0x60
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x15
	.ascii	"hit\000"
	.byte	0xf
	.byte	0x61
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF107
	.byte	0xf
	.byte	0x62
	.4byte	0x537
	.byte	0x8
	.byte	0
	.uleb128 0x13
	.4byte	.LASF108
	.byte	0x24
	.byte	0xf
	.byte	0x65
	.4byte	0x6de
	.uleb128 0x14
	.4byte	.LASF94
	.byte	0xf
	.byte	0x67
	.4byte	0x30a
	.byte	0
	.uleb128 0x14
	.4byte	.LASF109
	.byte	0xf
	.byte	0x68
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x69
	.4byte	0x30a
	.byte	0x8
	.uleb128 0x15
	.ascii	"len\000"
	.byte	0xf
	.byte	0x6a
	.4byte	0x30a
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF110
	.byte	0xf
	.byte	0x6b
	.4byte	0x30a
	.byte	0x10
	.uleb128 0x14
	.4byte	.LASF111
	.byte	0xf
	.byte	0x6c
	.4byte	0x30a
	.byte	0x14
	.uleb128 0x14
	.4byte	.LASF112
	.byte	0xf
	.byte	0x6d
	.4byte	0x6de
	.byte	0x18
	.uleb128 0x15
	.ascii	"tbl\000"
	.byte	0xf
	.byte	0x6e
	.4byte	0x6ee
	.byte	0x20
	.byte	0
	.uleb128 0xb
	.4byte	0x30a
	.4byte	0x6ee
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1
	.byte	0
	.uleb128 0xb
	.4byte	0x2ff
	.4byte	0x6fe
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1
	.byte	0
	.uleb128 0x13
	.4byte	.LASF113
	.byte	0x10
	.byte	0xf
	.byte	0x72
	.4byte	0x75e
	.uleb128 0x14
	.4byte	.LASF94
	.byte	0xf
	.byte	0x74
	.4byte	0x2ff
	.byte	0
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0x75
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x76
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF86
	.byte	0xf
	.byte	0x77
	.4byte	0x2ff
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF114
	.byte	0xf
	.byte	0x78
	.4byte	0x2ff
	.byte	0xa
	.uleb128 0x14
	.4byte	.LASF115
	.byte	0xf
	.byte	0x79
	.4byte	0x2ff
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF112
	.byte	0xf
	.byte	0x7a
	.4byte	0x75e
	.byte	0xe
	.byte	0
	.uleb128 0xb
	.4byte	0x2f4
	.4byte	0x76e
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1
	.byte	0
	.uleb128 0x13
	.4byte	.LASF116
	.byte	0x10
	.byte	0xf
	.byte	0x7d
	.4byte	0x7b6
	.uleb128 0x14
	.4byte	.LASF94
	.byte	0xf
	.byte	0x7f
	.4byte	0x2ff
	.byte	0
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0x80
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x81
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x15
	.ascii	"lpa\000"
	.byte	0xf
	.byte	0x82
	.4byte	0x30a
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF117
	.byte	0xf
	.byte	0x83
	.4byte	0x30a
	.byte	0xc
	.byte	0
	.uleb128 0x13
	.4byte	.LASF118
	.byte	0x10
	.byte	0xf
	.byte	0x8f
	.4byte	0x7fe
	.uleb128 0x14
	.4byte	.LASF94
	.byte	0xf
	.byte	0x91
	.4byte	0x2ff
	.byte	0
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0x92
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x93
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF119
	.byte	0xf
	.byte	0x94
	.4byte	0x2ff
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF112
	.byte	0xf
	.byte	0x95
	.4byte	0x7fe
	.byte	0xa
	.byte	0
	.uleb128 0xb
	.4byte	0x2f4
	.4byte	0x80e
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x5
	.byte	0
	.uleb128 0x13
	.4byte	.LASF120
	.byte	0x10
	.byte	0xf
	.byte	0x98
	.4byte	0x856
	.uleb128 0x14
	.4byte	.LASF94
	.byte	0xf
	.byte	0x9a
	.4byte	0x2ff
	.byte	0
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0x9b
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF87
	.byte	0xf
	.byte	0x9c
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF121
	.byte	0xf
	.byte	0x9d
	.4byte	0x30a
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF122
	.byte	0xf
	.byte	0x9e
	.4byte	0x30a
	.byte	0xc
	.byte	0
	.uleb128 0x13
	.4byte	.LASF123
	.byte	0x30
	.byte	0xf
	.byte	0xa1
	.4byte	0x953
	.uleb128 0x15
	.ascii	"tag\000"
	.byte	0xf
	.byte	0xa3
	.4byte	0x30a
	.byte	0
	.uleb128 0x15
	.ascii	"ver\000"
	.byte	0xf
	.byte	0xa4
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF91
	.byte	0xf
	.byte	0xa6
	.4byte	0x2ff
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF124
	.byte	0xf
	.byte	0xa7
	.4byte	0x2f4
	.byte	0xa
	.uleb128 0x14
	.4byte	.LASF125
	.byte	0xf
	.byte	0xa8
	.4byte	0x2f4
	.byte	0xb
	.uleb128 0x14
	.4byte	.LASF126
	.byte	0xf
	.byte	0xa9
	.4byte	0x2f4
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF127
	.byte	0xf
	.byte	0xaa
	.4byte	0x2f4
	.byte	0xd
	.uleb128 0x14
	.4byte	.LASF128
	.byte	0xf
	.byte	0xab
	.4byte	0x2ff
	.byte	0xe
	.uleb128 0x14
	.4byte	.LASF129
	.byte	0xf
	.byte	0xad
	.4byte	0x2ff
	.byte	0x10
	.uleb128 0x14
	.4byte	.LASF130
	.byte	0xf
	.byte	0xae
	.4byte	0x2ff
	.byte	0x12
	.uleb128 0x14
	.4byte	.LASF131
	.byte	0xf
	.byte	0xaf
	.4byte	0x2ff
	.byte	0x14
	.uleb128 0x14
	.4byte	.LASF132
	.byte	0xf
	.byte	0xb0
	.4byte	0x2ff
	.byte	0x16
	.uleb128 0x14
	.4byte	.LASF133
	.byte	0xf
	.byte	0xb2
	.4byte	0x2ff
	.byte	0x18
	.uleb128 0x14
	.4byte	.LASF134
	.byte	0xf
	.byte	0xb3
	.4byte	0x2ff
	.byte	0x1a
	.uleb128 0x14
	.4byte	.LASF135
	.byte	0xf
	.byte	0xb4
	.4byte	0x2ff
	.byte	0x1c
	.uleb128 0x14
	.4byte	.LASF136
	.byte	0xf
	.byte	0xb5
	.4byte	0x2ff
	.byte	0x1e
	.uleb128 0x14
	.4byte	.LASF137
	.byte	0xf
	.byte	0xb7
	.4byte	0x30a
	.byte	0x20
	.uleb128 0x14
	.4byte	.LASF138
	.byte	0xf
	.byte	0xb8
	.4byte	0x30a
	.byte	0x24
	.uleb128 0x14
	.4byte	.LASF139
	.byte	0xf
	.byte	0xb9
	.4byte	0x30a
	.byte	0x28
	.uleb128 0x14
	.4byte	.LASF112
	.byte	0xf
	.byte	0xba
	.4byte	0x6ee
	.byte	0x2c
	.byte	0
	.uleb128 0x17
	.4byte	.LASF140
	.2byte	0x200
	.byte	0xf
	.byte	0xbe
	.4byte	0xa99
	.uleb128 0x15
	.ascii	"tag\000"
	.byte	0xf
	.byte	0xc0
	.4byte	0x30a
	.byte	0
	.uleb128 0x15
	.ascii	"ver\000"
	.byte	0xf
	.byte	0xc1
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF141
	.byte	0xf
	.byte	0xc3
	.4byte	0x30a
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF142
	.byte	0xf
	.byte	0xc4
	.4byte	0x30a
	.byte	0xc
	.uleb128 0x14
	.4byte	.LASF143
	.byte	0xf
	.byte	0xc5
	.4byte	0x30a
	.byte	0x10
	.uleb128 0x14
	.4byte	.LASF144
	.byte	0xf
	.byte	0xc6
	.4byte	0x30a
	.byte	0x14
	.uleb128 0x14
	.4byte	.LASF137
	.byte	0xf
	.byte	0xc8
	.4byte	0x30a
	.byte	0x18
	.uleb128 0x14
	.4byte	.LASF145
	.byte	0xf
	.byte	0xc9
	.4byte	0x30a
	.byte	0x1c
	.uleb128 0x14
	.4byte	.LASF146
	.byte	0xf
	.byte	0xca
	.4byte	0x30a
	.byte	0x20
	.uleb128 0x14
	.4byte	.LASF147
	.byte	0xf
	.byte	0xcb
	.4byte	0x30a
	.byte	0x24
	.uleb128 0x14
	.4byte	.LASF134
	.byte	0xf
	.byte	0xcd
	.4byte	0x30a
	.byte	0x28
	.uleb128 0x14
	.4byte	.LASF148
	.byte	0xf
	.byte	0xce
	.4byte	0x30a
	.byte	0x2c
	.uleb128 0x14
	.4byte	.LASF149
	.byte	0xf
	.byte	0xcf
	.4byte	0x30a
	.byte	0x30
	.uleb128 0x14
	.4byte	.LASF150
	.byte	0xf
	.byte	0xd0
	.4byte	0x30a
	.byte	0x34
	.uleb128 0x14
	.4byte	.LASF151
	.byte	0xf
	.byte	0xd2
	.4byte	0x30a
	.byte	0x38
	.uleb128 0x14
	.4byte	.LASF152
	.byte	0xf
	.byte	0xd3
	.4byte	0x30a
	.byte	0x3c
	.uleb128 0x14
	.4byte	.LASF153
	.byte	0xf
	.byte	0xd4
	.4byte	0x30a
	.byte	0x40
	.uleb128 0x14
	.4byte	.LASF154
	.byte	0xf
	.byte	0xd5
	.4byte	0x30a
	.byte	0x44
	.uleb128 0x14
	.4byte	.LASF155
	.byte	0xf
	.byte	0xd7
	.4byte	0x30a
	.byte	0x48
	.uleb128 0x14
	.4byte	.LASF156
	.byte	0xf
	.byte	0xd8
	.4byte	0x30a
	.byte	0x4c
	.uleb128 0x14
	.4byte	.LASF157
	.byte	0xf
	.byte	0xd9
	.4byte	0x30a
	.byte	0x50
	.uleb128 0x14
	.4byte	.LASF158
	.byte	0xf
	.byte	0xda
	.4byte	0x30a
	.byte	0x54
	.uleb128 0x14
	.4byte	.LASF159
	.byte	0xf
	.byte	0xdc
	.4byte	0x30a
	.byte	0x58
	.uleb128 0x14
	.4byte	.LASF160
	.byte	0xf
	.byte	0xdd
	.4byte	0x30a
	.byte	0x5c
	.uleb128 0x14
	.4byte	.LASF161
	.byte	0xf
	.byte	0xde
	.4byte	0x30a
	.byte	0x60
	.uleb128 0x14
	.4byte	.LASF112
	.byte	0xf
	.byte	0xe0
	.4byte	0xa99
	.byte	0x64
	.byte	0
	.uleb128 0xb
	.4byte	0x30a
	.4byte	0xaa9
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x66
	.byte	0
	.uleb128 0x13
	.4byte	.LASF162
	.byte	0x8
	.byte	0xf
	.byte	0xe4
	.4byte	0xada
	.uleb128 0x14
	.4byte	.LASF163
	.byte	0xf
	.byte	0xe6
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF164
	.byte	0xf
	.byte	0xe7
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF165
	.byte	0xf
	.byte	0xe8
	.4byte	0x2ff
	.byte	0x4
	.byte	0
	.uleb128 0x17
	.4byte	.LASF166
	.2byte	0x808
	.byte	0xf
	.byte	0xeb
	.4byte	0xb24
	.uleb128 0x15
	.ascii	"max\000"
	.byte	0xf
	.byte	0xed
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF167
	.byte	0xf
	.byte	0xee
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF168
	.byte	0xf
	.byte	0xef
	.4byte	0x2ff
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF114
	.byte	0xf
	.byte	0xf0
	.4byte	0x2ff
	.byte	0x6
	.uleb128 0x15
	.ascii	"arr\000"
	.byte	0xf
	.byte	0xf1
	.4byte	0xb24
	.byte	0x8
	.byte	0
	.uleb128 0xb
	.4byte	0x2ff
	.4byte	0xb35
	.uleb128 0x18
	.4byte	0x2c
	.2byte	0x3ff
	.byte	0
	.uleb128 0x13
	.4byte	.LASF169
	.byte	0x30
	.byte	0xf
	.byte	0xf5
	.4byte	0xbc7
	.uleb128 0x15
	.ascii	"id\000"
	.byte	0xf
	.byte	0xf7
	.4byte	0x2ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF170
	.byte	0xf
	.byte	0xf8
	.4byte	0x2ff
	.byte	0x2
	.uleb128 0x14
	.4byte	.LASF171
	.byte	0xf
	.byte	0xf9
	.4byte	0x2ff
	.byte	0x4
	.uleb128 0x14
	.4byte	.LASF172
	.byte	0xf
	.byte	0xfa
	.4byte	0x2f4
	.byte	0x6
	.uleb128 0x14
	.4byte	.LASF173
	.byte	0xf
	.byte	0xfb
	.4byte	0x2f4
	.byte	0x7
	.uleb128 0x14
	.4byte	.LASF174
	.byte	0xf
	.byte	0xfc
	.4byte	0x2f4
	.byte	0x8
	.uleb128 0x14
	.4byte	.LASF175
	.byte	0xf
	.byte	0xfd
	.4byte	0x2f4
	.byte	0x9
	.uleb128 0x14
	.4byte	.LASF176
	.byte	0xf
	.byte	0xfe
	.4byte	0x2f4
	.byte	0xa
	.uleb128 0x14
	.4byte	.LASF177
	.byte	0xf
	.byte	0xff
	.4byte	0x2f4
	.byte	0xb
	.uleb128 0x19
	.4byte	.LASF87
	.byte	0xf
	.2byte	0x100
	.4byte	0x30a
	.byte	0xc
	.uleb128 0x19
	.4byte	.LASF178
	.byte	0xf
	.2byte	0x101
	.4byte	0xbc7
	.byte	0x10
	.byte	0
	.uleb128 0xb
	.4byte	0x2ff
	.4byte	0xbd7
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xf
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF179
	.byte	0xc
	.byte	0xf
	.2byte	0x105
	.4byte	0xc0c
	.uleb128 0x19
	.4byte	.LASF180
	.byte	0xf
	.2byte	0x107
	.4byte	0x30a
	.byte	0
	.uleb128 0x19
	.4byte	.LASF181
	.byte	0xf
	.2byte	0x108
	.4byte	0x30a
	.byte	0x4
	.uleb128 0x1b
	.ascii	"lpa\000"
	.byte	0xf
	.2byte	0x109
	.4byte	0x30a
	.byte	0x8
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF182
	.byte	0xc
	.byte	0xf
	.2byte	0x10c
	.4byte	0xc41
	.uleb128 0x19
	.4byte	.LASF63
	.byte	0xf
	.2byte	0x10e
	.4byte	0x537
	.byte	0
	.uleb128 0x19
	.4byte	.LASF64
	.byte	0xf
	.2byte	0x10f
	.4byte	0x537
	.byte	0x4
	.uleb128 0x19
	.4byte	.LASF183
	.byte	0xf
	.2byte	0x110
	.4byte	0x30a
	.byte	0x8
	.byte	0
	.uleb128 0x1c
	.4byte	.LASF184
	.byte	0x10
	.byte	0x12
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_type
	.uleb128 0x1c
	.4byte	.LASF185
	.byte	0x10
	.byte	0x13
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_die_num
	.uleb128 0x1c
	.4byte	.LASF186
	.byte	0x10
	.byte	0x14
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_planes_per_die
	.uleb128 0x1c
	.4byte	.LASF187
	.byte	0x10
	.byte	0x15
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blks_per_die
	.uleb128 0x1c
	.4byte	.LASF188
	.byte	0x10
	.byte	0x16
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blks_per_die_shift
	.uleb128 0x1c
	.4byte	.LASF189
	.byte	0x10
	.byte	0x17
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_planes_num
	.uleb128 0x1c
	.4byte	.LASF190
	.byte	0x10
	.byte	0x18
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blk_pre_plane
	.uleb128 0x1c
	.4byte	.LASF191
	.byte	0x10
	.byte	0x19
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_ext_blk_pre_plane
	.uleb128 0x1c
	.4byte	.LASF192
	.byte	0x10
	.byte	0x1a
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_bbm_buf_size
	.uleb128 0x1c
	.4byte	.LASF193
	.byte	0x10
	.byte	0x1c
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_blk
	.uleb128 0x1c
	.4byte	.LASF194
	.byte	0x10
	.byte	0x1d
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_slc_blk
	.uleb128 0x1c
	.4byte	.LASF195
	.byte	0x10
	.byte	0x1e
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_super_blk
	.uleb128 0x1c
	.4byte	.LASF196
	.byte	0x10
	.byte	0x1f
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sec_pre_page
	.uleb128 0x1c
	.4byte	.LASF197
	.byte	0x10
	.byte	0x20
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sec_pre_page_shift
	.uleb128 0x1c
	.4byte	.LASF198
	.byte	0x10
	.byte	0x24
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_byte_pre_page
	.uleb128 0x1c
	.4byte	.LASF199
	.byte	0x10
	.byte	0x25
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_byte_pre_oob
	.uleb128 0x1c
	.4byte	.LASF200
	.byte	0x10
	.byte	0x26
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_reserved_blks
	.uleb128 0x1c
	.4byte	.LASF201
	.byte	0x10
	.byte	0x27
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_totle_phy_blks
	.uleb128 0x1c
	.4byte	.LASF202
	.byte	0x10
	.byte	0x29
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_map_region_num
	.uleb128 0x1c
	.4byte	.LASF203
	.byte	0x10
	.byte	0x2a
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_l2pmap_ram_region_num
	.uleb128 0x1c
	.4byte	.LASF204
	.byte	0x10
	.byte	0x2c
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_vendor_region_num
	.uleb128 0x1c
	.4byte	.LASF205
	.byte	0x10
	.byte	0x2e
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_map_blks_per_plane
	.uleb128 0x1c
	.4byte	.LASF206
	.byte	0x10
	.byte	0x2f
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_map_blks
	.uleb128 0x1c
	.4byte	.LASF207
	.byte	0x10
	.byte	0x30
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_vendor_blks
	.uleb128 0x1c
	.4byte	.LASF208
	.byte	0x10
	.byte	0x31
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_vendor_part_size
	.uleb128 0x1c
	.4byte	.LASF209
	.byte	0x10
	.byte	0x32
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sys_blks_per_plane
	.uleb128 0x1c
	.4byte	.LASF210
	.byte	0x10
	.byte	0x33
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_init_sys_blks_per_plane
	.uleb128 0x1c
	.4byte	.LASF211
	.byte	0x10
	.byte	0x34
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_sys_blks
	.uleb128 0x1c
	.4byte	.LASF212
	.byte	0x10
	.byte	0x35
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_data_blks_per_plane
	.uleb128 0x1c
	.4byte	.LASF213
	.byte	0x10
	.byte	0x36
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_data_op_blks_per_plane
	.uleb128 0x1c
	.4byte	.LASF214
	.byte	0x10
	.byte	0x37
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_data_blks
	.uleb128 0x1c
	.4byte	.LASF215
	.byte	0x10
	.byte	0x38
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_gc_temp_power_lost_recovery_flag
	.uleb128 0x1c
	.4byte	.LASF216
	.byte	0x10
	.byte	0x3a
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_GlobalSysVersion
	.uleb128 0x1c
	.4byte	.LASF217
	.byte	0x10
	.byte	0x3b
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_GlobalDataVersion
	.uleb128 0x1c
	.4byte	.LASF218
	.byte	0x10
	.byte	0x3c
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLbaSector
	.uleb128 0x1c
	.4byte	.LASF219
	.byte	0x10
	.byte	0x3d
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLpn
	.uleb128 0x1c
	.4byte	.LASF220
	.byte	0x10
	.byte	0x3e
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_VaildLpn
	.uleb128 0x1c
	.4byte	.LASF221
	.byte	0x10
	.byte	0x3f
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLbn
	.uleb128 0x1c
	.4byte	.LASF222
	.byte	0x10
	.byte	0x41
	.4byte	0x4b7
	.uleb128 0x5
	.byte	0x3
	.4byte	gBbtInfo
	.uleb128 0x1c
	.4byte	.LASF223
	.byte	0x10
	.byte	0x42
	.4byte	0x53d
	.uleb128 0x5
	.byte	0x3
	.4byte	gSysInfo
	.uleb128 0x1c
	.4byte	.LASF224
	.byte	0x10
	.byte	0x43
	.4byte	0xada
	.uleb128 0x5
	.byte	0x3
	.4byte	gSysFreeQueue
	.uleb128 0x1c
	.4byte	.LASF225
	.byte	0x10
	.byte	0x44
	.4byte	0x585
	.uleb128 0x5
	.byte	0x3
	.4byte	gL2pMapInfo
	.uleb128 0x1c
	.4byte	.LASF226
	.byte	0x10
	.byte	0x45
	.4byte	0x585
	.uleb128 0x5
	.byte	0x3
	.4byte	gVendorBlkInfo
	.uleb128 0x1c
	.4byte	.LASF227
	.byte	0x10
	.byte	0x46
	.4byte	0x32b
	.uleb128 0x5
	.byte	0x3
	.4byte	req_sys
	.uleb128 0x1c
	.4byte	.LASF228
	.byte	0x10
	.byte	0x47
	.4byte	0xf3e
	.uleb128 0x5
	.byte	0x3
	.4byte	req_read
	.uleb128 0x7
	.byte	0x4
	.4byte	0x32b
	.uleb128 0x1c
	.4byte	.LASF229
	.byte	0x10
	.byte	0x48
	.4byte	0xf3e
	.uleb128 0x5
	.byte	0x3
	.4byte	req_prgm
	.uleb128 0x1c
	.4byte	.LASF230
	.byte	0x10
	.byte	0x49
	.4byte	0xf3e
	.uleb128 0x5
	.byte	0x3
	.4byte	req_erase
	.uleb128 0x1c
	.4byte	.LASF231
	.byte	0x10
	.byte	0x4a
	.4byte	0xf3e
	.uleb128 0x5
	.byte	0x3
	.4byte	req_gc
	.uleb128 0x1c
	.4byte	.LASF232
	.byte	0x10
	.byte	0x4b
	.4byte	0xf3e
	.uleb128 0x5
	.byte	0x3
	.4byte	req_gc_dst
	.uleb128 0x1c
	.4byte	.LASF233
	.byte	0x10
	.byte	0x4d
	.4byte	0xf3e
	.uleb128 0x5
	.byte	0x3
	.4byte	g_req_cache
	.uleb128 0xb
	.4byte	0x2f4
	.4byte	0xfa9
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1f
	.byte	0
	.uleb128 0x1c
	.4byte	.LASF234
	.byte	0x10
	.byte	0x4f
	.4byte	0xf99
	.uleb128 0x5
	.byte	0x3
	.4byte	p_plane_order_table
	.uleb128 0x1c
	.4byte	.LASF235
	.byte	0x10
	.byte	0x51
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_data_buf
	.uleb128 0x1c
	.4byte	.LASF236
	.byte	0x10
	.byte	0x52
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_data_buf_1
	.uleb128 0x1c
	.4byte	.LASF237
	.byte	0x10
	.byte	0x53
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_data_buf
	.uleb128 0x1c
	.4byte	.LASF238
	.byte	0x10
	.byte	0x54
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_spare_buf
	.uleb128 0x1c
	.4byte	.LASF239
	.byte	0x10
	.byte	0x55
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_data_buf_0
	.uleb128 0x1c
	.4byte	.LASF240
	.byte	0x10
	.byte	0x56
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_data_buf_1
	.uleb128 0x1c
	.4byte	.LASF241
	.byte	0x10
	.byte	0x57
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_spare_buf
	.uleb128 0x1c
	.4byte	.LASF242
	.byte	0x10
	.byte	0x58
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_spare_buf
	.uleb128 0x1c
	.4byte	.LASF243
	.byte	0x10
	.byte	0x59
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_data_buf
	.uleb128 0x1c
	.4byte	.LASF244
	.byte	0x10
	.byte	0x5a
	.4byte	0x1064
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_gc_page_buf_info
	.uleb128 0x7
	.byte	0x4
	.4byte	0xc0c
	.uleb128 0x1c
	.4byte	.LASF245
	.byte	0x10
	.byte	0x5b
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	c_gc_page_buf_num
	.uleb128 0x1c
	.4byte	.LASF246
	.byte	0x10
	.byte	0x5c
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_num_req
	.uleb128 0x1c
	.4byte	.LASF247
	.byte	0x10
	.byte	0x5d
	.4byte	0x109d
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_ect_tbl_info
	.uleb128 0x7
	.byte	0x4
	.4byte	0x671
	.uleb128 0x1c
	.4byte	.LASF248
	.byte	0x10
	.byte	0x5e
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_info_size
	.uleb128 0x1c
	.4byte	.LASF249
	.byte	0x10
	.byte	0x5f
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_erase_count_table
	.uleb128 0x1c
	.4byte	.LASF250
	.byte	0x10
	.byte	0x61
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_swl_mul_table
	.uleb128 0x1c
	.4byte	.LASF251
	.byte	0x10
	.byte	0x62
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_swl_count
	.uleb128 0x1c
	.4byte	.LASF252
	.byte	0x10
	.byte	0x63
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_valid_page_count_table
	.uleb128 0x1c
	.4byte	.LASF253
	.byte	0x10
	.byte	0x64
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_valid_page_count_check_table
	.uleb128 0x1c
	.4byte	.LASF254
	.byte	0x10
	.byte	0x65
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_blk_mode_table
	.uleb128 0x1c
	.4byte	.LASF255
	.byte	0x10
	.byte	0x67
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_table
	.uleb128 0x1c
	.4byte	.LASF256
	.byte	0x10
	.byte	0x68
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_valid_page_count
	.uleb128 0x1c
	.4byte	.LASF257
	.byte	0x10
	.byte	0x69
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_ver_table
	.uleb128 0x1c
	.4byte	.LASF258
	.byte	0x10
	.byte	0x6a
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_region_ppn_table
	.uleb128 0x1c
	.4byte	.LASF259
	.byte	0x10
	.byte	0x6b
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_map_block
	.uleb128 0x1c
	.4byte	.LASF260
	.byte	0x10
	.byte	0x6d
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_table
	.uleb128 0x1c
	.4byte	.LASF261
	.byte	0x10
	.byte	0x6e
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_valid_page_count
	.uleb128 0x1c
	.4byte	.LASF262
	.byte	0x10
	.byte	0x6f
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_ver_table
	.uleb128 0x1c
	.4byte	.LASF263
	.byte	0x10
	.byte	0x70
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_region_ppn_table
	.uleb128 0x1c
	.4byte	.LASF264
	.byte	0x10
	.byte	0x71
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_vendor_block
	.uleb128 0x1c
	.4byte	.LASF265
	.byte	0x10
	.byte	0x73
	.4byte	0x11d5
	.uleb128 0x5
	.byte	0x3
	.4byte	p_l2p_ram_map
	.uleb128 0x7
	.byte	0x4
	.4byte	0x634
	.uleb128 0x1c
	.4byte	.LASF266
	.byte	0x10
	.byte	0x74
	.4byte	0x537
	.uleb128 0x5
	.byte	0x3
	.4byte	p_l2p_map_buf
	.uleb128 0x1c
	.4byte	.LASF267
	.byte	0x10
	.byte	0x75
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_l2p_last_update_region_id
	.uleb128 0x1c
	.4byte	.LASF268
	.byte	0x10
	.byte	0x7c
	.4byte	0x120e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_table
	.uleb128 0x7
	.byte	0x4
	.4byte	0xaa9
	.uleb128 0x1c
	.4byte	.LASF269
	.byte	0x10
	.byte	0x7d
	.4byte	0x120e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_free_data_block_list_head
	.uleb128 0x1c
	.4byte	.LASF270
	.byte	0x10
	.byte	0x7f
	.4byte	0x120e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_head
	.uleb128 0x1c
	.4byte	.LASF271
	.byte	0x10
	.byte	0x80
	.4byte	0x120e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_tail
	.uleb128 0x1c
	.4byte	.LASF272
	.byte	0x10
	.byte	0x81
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_num_free_superblocks
	.uleb128 0x1c
	.4byte	.LASF273
	.byte	0x10
	.byte	0x82
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_num_data_superblocks
	.uleb128 0x1c
	.4byte	.LASF274
	.byte	0x10
	.byte	0x83
	.4byte	0xb35
	.uleb128 0x5
	.byte	0x3
	.4byte	g_active_superblock
	.uleb128 0x1c
	.4byte	.LASF275
	.byte	0x10
	.byte	0x84
	.4byte	0xb35
	.uleb128 0x5
	.byte	0x3
	.4byte	g_buffer_superblock
	.uleb128 0x1c
	.4byte	.LASF276
	.byte	0x10
	.byte	0x85
	.4byte	0xb35
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_temp_superblock
	.uleb128 0x1c
	.4byte	.LASF277
	.byte	0x10
	.byte	0x86
	.4byte	0xb35
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_superblock
	.uleb128 0x1c
	.4byte	.LASF278
	.byte	0x10
	.byte	0x87
	.4byte	0x12be
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_last_act_superblock
	.uleb128 0x7
	.byte	0x4
	.4byte	0xb35
	.uleb128 0x1c
	.4byte	.LASF279
	.byte	0x10
	.byte	0x88
	.4byte	0x856
	.uleb128 0x5
	.byte	0x3
	.4byte	g_sys_save_data
	.uleb128 0x1c
	.4byte	.LASF280
	.byte	0x10
	.byte	0x89
	.4byte	0x953
	.uleb128 0x5
	.byte	0x3
	.4byte	g_sys_ext_data
	.uleb128 0x1c
	.4byte	.LASF281
	.byte	0x10
	.byte	0x8b
	.4byte	0x12f7
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_page_info
	.uleb128 0x7
	.byte	0x4
	.4byte	0xbd7
	.uleb128 0x1c
	.4byte	.LASF282
	.byte	0x10
	.byte	0x8c
	.4byte	0x62e
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_blk_tbl
	.uleb128 0x1c
	.4byte	.LASF283
	.byte	0x10
	.byte	0x8d
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_blk_num
	.uleb128 0x1c
	.4byte	.LASF284
	.byte	0x10
	.byte	0x8e
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_page_offset
	.uleb128 0x1c
	.4byte	.LASF285
	.byte	0x10
	.byte	0x8f
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_cur_blk_valid_pages
	.uleb128 0x1c
	.4byte	.LASF286
	.byte	0x10
	.byte	0x90
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_cur_blk_max_valid_pages
	.uleb128 0x1c
	.4byte	.LASF287
	.byte	0x10
	.byte	0x91
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_next_blk
	.uleb128 0x1c
	.4byte	.LASF288
	.byte	0x10
	.byte	0x92
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_next_blk_1
	.uleb128 0x1c
	.4byte	.LASF289
	.byte	0x10
	.byte	0x93
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_temp_num
	.uleb128 0x1c
	.4byte	.LASF290
	.byte	0x10
	.byte	0x94
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_gc_index
	.uleb128 0xb
	.4byte	0x2ff
	.4byte	0x13a6
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x10
	.byte	0
	.uleb128 0x1c
	.4byte	.LASF291
	.byte	0x10
	.byte	0x95
	.4byte	0x1396
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_temp_tbl
	.uleb128 0x1c
	.4byte	.LASF292
	.byte	0x10
	.byte	0x97
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_free_blk_threshold
	.uleb128 0x1c
	.4byte	.LASF293
	.byte	0x10
	.byte	0x98
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_merge_free_blk_threshold
	.uleb128 0x1c
	.4byte	.LASF294
	.byte	0x10
	.byte	0x99
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_blk_index
	.uleb128 0x1c
	.4byte	.LASF295
	.byte	0x10
	.byte	0x9b
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_gc_page_count
	.uleb128 0x1c
	.4byte	.LASF296
	.byte	0x10
	.byte	0x9c
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_write_page_count
	.uleb128 0x1c
	.4byte	.LASF297
	.byte	0x10
	.byte	0x9d
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_write_sector
	.uleb128 0x1c
	.4byte	.LASF298
	.byte	0x10
	.byte	0x9e
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_read_sector
	.uleb128 0x1c
	.4byte	.LASF299
	.byte	0x10
	.byte	0xa0
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_discard_page_count
	.uleb128 0x1c
	.4byte	.LASF300
	.byte	0x10
	.byte	0xa1
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_read_page_count
	.uleb128 0x1c
	.4byte	.LASF301
	.byte	0x10
	.byte	0xa2
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_tmp_data_superblock_id
	.uleb128 0x1c
	.4byte	.LASF302
	.byte	0x10
	.byte	0xa3
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_cache_write_count
	.uleb128 0x1c
	.4byte	.LASF303
	.byte	0x10
	.byte	0xa4
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_l2p_write_count
	.uleb128 0x1c
	.4byte	.LASF304
	.byte	0x10
	.byte	0xa6
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_mlc_erase_count
	.uleb128 0x1c
	.4byte	.LASF305
	.byte	0x10
	.byte	0xa7
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_avg_erase_count
	.uleb128 0x1c
	.4byte	.LASF306
	.byte	0x10
	.byte	0xa8
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_min_erase_count
	.uleb128 0x1c
	.4byte	.LASF307
	.byte	0x10
	.byte	0xa9
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_slc_erase_count
	.uleb128 0x1c
	.4byte	.LASF308
	.byte	0x10
	.byte	0xaa
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_sys_slc_erase_count
	.uleb128 0x1c
	.4byte	.LASF309
	.byte	0x10
	.byte	0xab
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_max_erase_count
	.uleb128 0x1c
	.4byte	.LASF310
	.byte	0x10
	.byte	0xac
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_in_gc_progress
	.uleb128 0x1c
	.4byte	.LASF311
	.byte	0x10
	.byte	0xad
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_in_swl_replace
	.uleb128 0x1c
	.4byte	.LASF312
	.byte	0x10
	.byte	0xae
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ftl_nand_free_count
	.uleb128 0x1c
	.4byte	.LASF313
	.byte	0x10
	.byte	0xaf
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_head_data_block
	.uleb128 0x1c
	.4byte	.LASF314
	.byte	0x10
	.byte	0xb0
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_head_data_block_count
	.uleb128 0x1c
	.4byte	.LASF315
	.byte	0x10
	.byte	0xb1
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_skip_write_count
	.uleb128 0x1c
	.4byte	.LASF316
	.byte	0x10
	.byte	0xb2
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_cur_erase_blk
	.uleb128 0x1c
	.4byte	.LASF317
	.byte	0x10
	.byte	0xb4
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_page_num
	.uleb128 0x1c
	.4byte	.LASF318
	.byte	0x10
	.byte	0xb5
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_page_min_ver
	.uleb128 0xb
	.4byte	0x30a
	.4byte	0x1592
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1f
	.byte	0
	.uleb128 0x1c
	.4byte	.LASF319
	.byte	0x10
	.byte	0xb6
	.4byte	0x1582
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_ppa_tbl
	.uleb128 0x1c
	.4byte	.LASF320
	.byte	0x10
	.byte	0xb7
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	c_mlc_erase_count_value
	.uleb128 0x1c
	.4byte	.LASF321
	.byte	0x10
	.byte	0xb8
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_power_lost_recovery_flag
	.uleb128 0x8
	.4byte	.LASF322
	.byte	0xd
	.byte	0x14
	.4byte	0x37a
	.uleb128 0x8
	.4byte	.LASF323
	.byte	0xd
	.byte	0x15
	.4byte	0x37a
	.uleb128 0x8
	.4byte	.LASF324
	.byte	0xd
	.byte	0x16
	.4byte	0x41c
	.uleb128 0xb
	.4byte	0x97
	.4byte	0x15f7
	.uleb128 0x18
	.4byte	0x2c
	.2byte	0x3ff
	.byte	0
	.uleb128 0x8
	.4byte	.LASF325
	.byte	0xd
	.byte	0x17
	.4byte	0x15e6
	.uleb128 0x1c
	.4byte	.LASF326
	.byte	0x3
	.byte	0x5
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	DeviceCapacity
	.uleb128 0x1c
	.4byte	.LASF327
	.byte	0x3
	.byte	0x6
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	gFtlInitStatus
	.uleb128 0x8
	.4byte	.LASF328
	.byte	0x3
	.byte	0x6c
	.4byte	0x30a
	.uleb128 0x8
	.4byte	.LASF329
	.byte	0x3
	.byte	0x6d
	.4byte	0x30a
	.uleb128 0x8
	.4byte	.LASF330
	.byte	0x3
	.byte	0x6e
	.4byte	0x30a
	.uleb128 0x8
	.4byte	.LASF331
	.byte	0x3
	.byte	0x6f
	.4byte	0x30a
	.uleb128 0x1d
	.4byte	.LASF332
	.byte	0x3
	.2byte	0x3c0
	.4byte	0x62
	.uleb128 0x5
	.byte	0x3
	.4byte	power_up_flag
	.uleb128 0x1e
	.4byte	.LASF336
	.byte	0x3
	.2byte	0x483
	.4byte	0x30a
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_discard_updated
	.uleb128 0x1d
	.4byte	.LASF333
	.byte	0x2
	.2byte	0x38c
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_power_up_flush
	.uleb128 0x1d
	.4byte	.LASF334
	.byte	0x2
	.2byte	0x3a1
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	FtlUpdateVaildLpnCount
	.uleb128 0xb
	.4byte	0x2ff
	.4byte	0x16a9
	.uleb128 0x18
	.4byte	0x2c
	.2byte	0x1fff
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF335
	.byte	0x2
	.2byte	0x8df
	.4byte	0x1698
	.uleb128 0x5
	.byte	0x3
	.4byte	check_vpc_table
	.uleb128 0x1f
	.4byte	.LASF337
	.byte	0x5
	.byte	0x6d
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_gc_temp_block_bops_scan_page_addr
	.uleb128 0x1d
	.4byte	.LASF338
	.byte	0x5
	.2byte	0x1d4
	.4byte	0x2ff
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_ink_free_return_value
	.uleb128 0x1f
	.4byte	.LASF339
	.byte	0x1
	.byte	0xe
	.4byte	0x15e6
	.uleb128 0x5
	.byte	0x3
	.4byte	check_buf
	.uleb128 0x20
	.4byte	0x15f7
	.byte	0x1
	.byte	0xf
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_temp_buf
	.uleb128 0xb
	.4byte	0x97
	.4byte	0x170c
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x7f
	.byte	0
	.uleb128 0x1f
	.4byte	.LASF340
	.byte	0x1
	.byte	0x10
	.4byte	0x16fc
	.uleb128 0x5
	.byte	0x3
	.4byte	check_spare_buf
	.uleb128 0x21
	.4byte	.LASF343
	.byte	0x1
	.byte	0xd2
	.4byte	0x62
	.4byte	.LFB279
	.4byte	.LFE279-.LFB279
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x176d
	.uleb128 0x22
	.4byte	.LASF341
	.byte	0x1
	.byte	0xd2
	.4byte	0x176d
	.4byte	.LLST290
	.uleb128 0x22
	.4byte	.LASF342
	.byte	0x1
	.byte	0xd2
	.4byte	0x176d
	.4byte	.LLST291
	.uleb128 0x22
	.4byte	.LASF114
	.byte	0x1
	.byte	0xd2
	.4byte	0x3f
	.4byte	.LLST292
	.uleb128 0x23
	.4byte	.LVL1636
	.4byte	0x67cf
	.byte	0
	.uleb128 0x24
	.byte	0x4
	.uleb128 0x21
	.4byte	.LASF344
	.byte	0x1
	.byte	0xcd
	.4byte	0x176d
	.4byte	.LFB278
	.4byte	.LFE278-.LFB278
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x17bf
	.uleb128 0x22
	.4byte	.LASF345
	.byte	0x1
	.byte	0xcd
	.4byte	0x176d
	.4byte	.LLST172
	.uleb128 0x22
	.4byte	.LASF346
	.byte	0x1
	.byte	0xcd
	.4byte	0x17bf
	.4byte	.LLST173
	.uleb128 0x22
	.4byte	.LASF347
	.byte	0x1
	.byte	0xcd
	.4byte	0x3f
	.4byte	.LLST174
	.uleb128 0x23
	.4byte	.LVL833
	.4byte	0x67da
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x17c5
	.uleb128 0x25
	.uleb128 0x26
	.4byte	.LASF350
	.byte	0x1
	.byte	0xc8
	.4byte	0x176d
	.byte	0x1
	.4byte	0x180a
	.uleb128 0x27
	.ascii	"s\000"
	.byte	0x1
	.byte	0xc8
	.4byte	0x176d
	.uleb128 0x27
	.ascii	"c\000"
	.byte	0x1
	.byte	0xc8
	.4byte	0x62
	.uleb128 0x27
	.ascii	"n\000"
	.byte	0x1
	.byte	0xc8
	.4byte	0x3f
	.uleb128 0x28
	.uleb128 0x29
	.ascii	"__p\000"
	.byte	0x1
	.byte	0xca
	.4byte	0x176d
	.uleb128 0x29
	.ascii	"__n\000"
	.byte	0x1
	.byte	0xca
	.4byte	0xcc
	.byte	0
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF348
	.byte	0x1
	.byte	0xb6
	.4byte	0x8c
	.4byte	.LFB276
	.4byte	.LFE276-.LFB276
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1900
	.uleb128 0x22
	.4byte	.LASF349
	.byte	0x1
	.byte	0xb6
	.4byte	0x46d
	.4byte	.LLST105
	.uleb128 0x2b
	.ascii	"die\000"
	.byte	0x1
	.byte	0xb6
	.4byte	0x97
	.4byte	.LLST106
	.uleb128 0x2c
	.4byte	.LASF114
	.byte	0x1
	.byte	0xb8
	.4byte	0x81
	.4byte	.LLST107
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x1
	.byte	0xb8
	.4byte	0x81
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB359
	.4byte	.Ldebug_ranges0+0x120
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
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x23
	.4byte	.LVL483
	.4byte	0x67e3
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	0x17c6
	.4byte	.LBB368
	.4byte	.LBE368-.LBB368
	.byte	0x1
	.byte	0xbd
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB369
	.4byte	.LBE369-.LBB369
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB370
	.4byte	.LBE370-.LBB370
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x23
	.4byte	.LVL487
	.4byte	0x67e3
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x26
	.4byte	.LASF351
	.byte	0x1
	.byte	0x9b
	.4byte	0x315
	.byte	0x1
	.4byte	0x1944
	.uleb128 0x33
	.4byte	.LASF352
	.byte	0x1
	.byte	0x9b
	.4byte	0x2ff
	.uleb128 0x29
	.ascii	"sts\000"
	.byte	0x1
	.byte	0x9d
	.4byte	0x315
	.uleb128 0x34
	.4byte	.LASF353
	.byte	0x1
	.byte	0x9e
	.4byte	0x1944
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x9f
	.4byte	0x32b
	.uleb128 0x35
	.ascii	"out\000"
	.byte	0x1
	.byte	0xb1
	.byte	0
	.uleb128 0xb
	.4byte	0x30a
	.4byte	0x1954
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xf
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF354
	.byte	0x1
	.byte	0x85
	.4byte	0x8c
	.4byte	.LFB274
	.4byte	.LFE274-.LFB274
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x19ea
	.uleb128 0x22
	.4byte	.LASF355
	.byte	0x1
	.byte	0x85
	.4byte	0x176d
	.4byte	.LLST91
	.uleb128 0x22
	.4byte	.LASF356
	.byte	0x1
	.byte	0x85
	.4byte	0x97
	.4byte	.LLST92
	.uleb128 0x22
	.4byte	.LASF357
	.byte	0x1
	.byte	0x85
	.4byte	0x97
	.4byte	.LLST93
	.uleb128 0x36
	.ascii	"i\000"
	.byte	0x1
	.byte	0x87
	.4byte	0x97
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x87
	.4byte	0x97
	.uleb128 0x34
	.4byte	.LASF61
	.byte	0x1
	.byte	0x87
	.4byte	0x97
	.uleb128 0x1f
	.4byte	.LASF358
	.byte	0x1
	.byte	0x87
	.4byte	0x97
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x88
	.4byte	0xf3e
	.uleb128 0x1f
	.4byte	.LASF359
	.byte	0x1
	.byte	0x89
	.4byte	0x97
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x23
	.4byte	.LVL404
	.4byte	0x1ba9
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF360
	.byte	0x1
	.byte	0x49
	.4byte	0x8c
	.4byte	.LFB273
	.4byte	.LFE273-.LFB273
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ae4
	.uleb128 0x22
	.4byte	.LASF355
	.byte	0x1
	.byte	0x49
	.4byte	0x176d
	.4byte	.LLST86
	.uleb128 0x22
	.4byte	.LASF357
	.byte	0x1
	.byte	0x49
	.4byte	0x97
	.4byte	.LLST87
	.uleb128 0x22
	.4byte	.LASF356
	.byte	0x1
	.byte	0x49
	.4byte	0x97
	.4byte	.LLST88
	.uleb128 0x22
	.4byte	.LASF361
	.byte	0x1
	.byte	0x49
	.4byte	0x97
	.4byte	.LLST89
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x1
	.byte	0x4b
	.4byte	0x97
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x4b
	.4byte	0x97
	.uleb128 0x34
	.4byte	.LASF61
	.byte	0x1
	.byte	0x4b
	.4byte	0x97
	.uleb128 0x1f
	.4byte	.LASF358
	.byte	0x1
	.byte	0x4b
	.4byte	0x97
	.uleb128 0x3
	.byte	0x91
	.sleb128 -68
	.uleb128 0x2c
	.4byte	.LASF359
	.byte	0x1
	.byte	0x4c
	.4byte	0x97
	.4byte	.LLST90
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x4d
	.4byte	0xf3e
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x1af4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4795
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0xa0
	.4byte	0x1ad1
	.uleb128 0x1f
	.4byte	.LASF363
	.byte	0x1
	.byte	0x68
	.4byte	0x32b
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x23
	.4byte	.LVL389
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL390
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL391
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL392
	.4byte	0x67ee
	.byte	0
	.uleb128 0x23
	.4byte	.LVL378
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL380
	.4byte	0x1ba9
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x1af4
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xe
	.byte	0
	.uleb128 0x3
	.4byte	0x1ae4
	.uleb128 0x2a
	.4byte	.LASF364
	.byte	0x1
	.byte	0x2b
	.4byte	0x8c
	.4byte	.LFB272
	.4byte	.LFE272-.LFB272
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ba9
	.uleb128 0x22
	.4byte	.LASF355
	.byte	0x1
	.byte	0x2b
	.4byte	0x176d
	.4byte	.LLST78
	.uleb128 0x22
	.4byte	.LASF357
	.byte	0x1
	.byte	0x2b
	.4byte	0x97
	.4byte	.LLST79
	.uleb128 0x22
	.4byte	.LASF356
	.byte	0x1
	.byte	0x2b
	.4byte	0x97
	.4byte	.LLST80
	.uleb128 0x36
	.ascii	"i\000"
	.byte	0x1
	.byte	0x2d
	.4byte	0x97
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x2d
	.4byte	0x97
	.uleb128 0x34
	.4byte	.LASF61
	.byte	0x1
	.byte	0x2d
	.4byte	0x97
	.uleb128 0x1f
	.4byte	.LASF358
	.byte	0x1
	.byte	0x2d
	.4byte	0x97
	.uleb128 0x2
	.byte	0x91
	.sleb128 -48
	.uleb128 0x2c
	.4byte	.LASF359
	.byte	0x1
	.byte	0x2e
	.4byte	0x97
	.4byte	.LLST81
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x2f
	.4byte	0xf3e
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x1af4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4778
	.uleb128 0x23
	.4byte	.LVL347
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL350
	.4byte	0x1ba9
	.byte	0
	.uleb128 0x39
	.4byte	.LASF667
	.byte	0x1
	.byte	0x11
	.4byte	0x97
	.4byte	.LFB271
	.4byte	.LFE271-.LFB271
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c51
	.uleb128 0x2b
	.ascii	"req\000"
	.byte	0x1
	.byte	0x11
	.4byte	0xf3e
	.4byte	.LLST0
	.uleb128 0x22
	.4byte	.LASF358
	.byte	0x1
	.byte	0x11
	.4byte	0x374
	.4byte	.LLST1
	.uleb128 0x22
	.4byte	.LASF365
	.byte	0x1
	.byte	0x11
	.4byte	0x374
	.4byte	.LLST2
	.uleb128 0x34
	.4byte	.LASF366
	.byte	0x1
	.byte	0x13
	.4byte	0x81
	.uleb128 0x34
	.4byte	.LASF367
	.byte	0x1
	.byte	0x13
	.4byte	0x81
	.uleb128 0x2c
	.4byte	.LASF71
	.byte	0x1
	.byte	0x14
	.4byte	0x81
	.4byte	.LLST3
	.uleb128 0x2c
	.4byte	.LASF72
	.byte	0x1
	.byte	0x14
	.4byte	0x81
	.4byte	.LLST4
	.uleb128 0x2c
	.4byte	.LASF368
	.byte	0x1
	.byte	0x15
	.4byte	0x97
	.4byte	.LLST5
	.uleb128 0x2c
	.4byte	.LASF62
	.byte	0x1
	.byte	0x16
	.4byte	0x97
	.4byte	.LLST6
	.uleb128 0x2c
	.4byte	.LASF369
	.byte	0x1
	.byte	0x17
	.4byte	0x97
	.4byte	.LLST7
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF370
	.byte	0x5
	.2byte	0x2f4
	.4byte	0x97
	.4byte	.LFB270
	.4byte	.LFE270-.LFB270
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c75
	.uleb128 0x23
	.4byte	.LVL1369
	.4byte	0x1c75
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF371
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x62
	.byte	0x1
	.4byte	0x1d42
	.uleb128 0x3c
	.4byte	.LASF372
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x30a
	.uleb128 0x3c
	.4byte	.LASF373
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF374
	.byte	0x5
	.2byte	0x1d7
	.4byte	0x2ff
	.uleb128 0x3e
	.ascii	"lpa\000"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x30a
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF375
	.byte	0x5
	.2byte	0x1d9
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF85
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x5
	.2byte	0x1da
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x5
	.2byte	0x1db
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x5
	.2byte	0x1dc
	.4byte	0x1d42
	.uleb128 0x3f
	.4byte	.LASF381
	.byte	0x5
	.2byte	0x272
	.uleb128 0x3f
	.4byte	.LASF382
	.byte	0x5
	.2byte	0x275
	.uleb128 0x40
	.4byte	.LASF362
	.4byte	0x1d58
	.4byte	.LASF371
	.uleb128 0x28
	.uleb128 0x3d
	.4byte	.LASF383
	.byte	0x5
	.2byte	0x1f3
	.4byte	0x2ff
	.byte	0
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x76e
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x1d58
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x16
	.byte	0
	.uleb128 0x3
	.4byte	0x1d48
	.uleb128 0x3b
	.4byte	.LASF384
	.byte	0x5
	.2byte	0x1a2
	.4byte	0x62
	.byte	0x1
	.4byte	0x1dbd
	.uleb128 0x3e
	.ascii	"ret\000"
	.byte	0x5
	.2byte	0x1a4
	.4byte	0x62
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x1a5
	.4byte	0x62
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x5
	.2byte	0x1a6
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF353
	.byte	0x5
	.2byte	0x1a7
	.4byte	0x1944
	.uleb128 0x41
	.4byte	0x1dae
	.uleb128 0x3d
	.4byte	.LASF385
	.byte	0x5
	.2byte	0x1b1
	.4byte	0x32b
	.byte	0
	.uleb128 0x28
	.uleb128 0x3d
	.4byte	.LASF386
	.byte	0x5
	.2byte	0x1c6
	.4byte	0x30a
	.byte	0
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF387
	.byte	0x5
	.2byte	0x185
	.4byte	0x62
	.byte	0x1
	.4byte	0x1e07
	.uleb128 0x3c
	.4byte	.LASF388
	.byte	0x5
	.2byte	0x185
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x5
	.2byte	0x187
	.4byte	0x2ff
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x188
	.4byte	0x2ff
	.uleb128 0x3e
	.ascii	"j\000"
	.byte	0x5
	.2byte	0x188
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x5
	.2byte	0x189
	.4byte	0x2ff
	.byte	0
	.uleb128 0x42
	.4byte	.LASF497
	.byte	0x5
	.2byte	0x17a
	.4byte	0x62
	.byte	0x1
	.uleb128 0x3a
	.4byte	.LASF389
	.byte	0x5
	.2byte	0x166
	.4byte	0x62
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1e70
	.uleb128 0x43
	.4byte	.LASF178
	.byte	0x5
	.2byte	0x166
	.4byte	0x2ff
	.4byte	.LLST77
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x168
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF390
	.byte	0x5
	.2byte	0x169
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL333
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL334
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL335
	.4byte	0x1e70
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF391
	.byte	0x5
	.2byte	0x158
	.4byte	0x62
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1ea4
	.uleb128 0x43
	.4byte	.LASF392
	.byte	0x5
	.2byte	0x158
	.4byte	0x2ff
	.4byte	.LLST76
	.uleb128 0x23
	.4byte	.LVL330
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF393
	.byte	0x5
	.2byte	0x13b
	.4byte	0x30a
	.4byte	.LFB263
	.4byte	.LFE263-.LFB263
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1f18
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x13d
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF380
	.byte	0x5
	.2byte	0x13e
	.4byte	0x1d42
	.4byte	.LLST244
	.uleb128 0x23
	.4byte	.LVL1274
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1275
	.4byte	0x22ce
	.uleb128 0x23
	.4byte	.LVL1276
	.4byte	0x20c7
	.uleb128 0x23
	.4byte	.LVL1277
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1278
	.4byte	0x493b
	.uleb128 0x23
	.4byte	.LVL1279
	.4byte	0x2096
	.uleb128 0x23
	.4byte	.LVL1281
	.4byte	0x21f0
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF394
	.byte	0x5
	.2byte	0x12b
	.4byte	0x30a
	.4byte	.LFB262
	.4byte	.LFE262-.LFB262
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1f72
	.uleb128 0x23
	.4byte	.LVL1282
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1283
	.4byte	0x20c7
	.uleb128 0x23
	.4byte	.LVL1284
	.4byte	0x2508
	.uleb128 0x23
	.4byte	.LVL1285
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1286
	.4byte	0x3736
	.uleb128 0x23
	.4byte	.LVL1287
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1288
	.4byte	0x23e7
	.byte	0
	.uleb128 0x45
	.4byte	.LASF453
	.byte	0x5
	.2byte	0x120
	.byte	0x1
	.uleb128 0x21
	.4byte	.LASF395
	.byte	0x5
	.byte	0xd2
	.4byte	0x30a
	.4byte	.LFB260
	.4byte	.LFE260-.LFB260
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2096
	.uleb128 0x22
	.4byte	.LASF396
	.byte	0x5
	.byte	0xd2
	.4byte	0x12be
	.4byte	.LLST163
	.uleb128 0x22
	.4byte	.LASF397
	.byte	0x5
	.byte	0xd2
	.4byte	0x30a
	.4byte	.LLST164
	.uleb128 0x2c
	.4byte	.LASF378
	.byte	0x5
	.byte	0xd4
	.4byte	0x2ff
	.4byte	.LLST165
	.uleb128 0x2c
	.4byte	.LASF357
	.byte	0x5
	.byte	0xd5
	.4byte	0x2ff
	.4byte	.LLST166
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x5
	.byte	0xd5
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF376
	.byte	0x5
	.byte	0xd6
	.4byte	0x2ff
	.uleb128 0x2c
	.4byte	.LASF172
	.byte	0x5
	.byte	0xd6
	.4byte	0x2ff
	.4byte	.LLST167
	.uleb128 0x2c
	.4byte	.LASF170
	.byte	0x5
	.byte	0xd6
	.4byte	0x2ff
	.4byte	.LLST168
	.uleb128 0x2c
	.4byte	.LASF398
	.byte	0x5
	.byte	0xd7
	.4byte	0x30a
	.4byte	.LLST169
	.uleb128 0x2c
	.4byte	.LASF399
	.byte	0x5
	.byte	0xd8
	.4byte	0x30a
	.4byte	.LLST170
	.uleb128 0x34
	.4byte	.LASF400
	.byte	0x5
	.byte	0xd9
	.4byte	0x30a
	.uleb128 0x2c
	.4byte	.LASF380
	.byte	0x5
	.byte	0xda
	.4byte	0x1d42
	.4byte	.LLST171
	.uleb128 0x46
	.4byte	.LASF401
	.byte	0x5
	.byte	0xe1
	.uleb128 0x46
	.4byte	.LASF402
	.byte	0x5
	.byte	0xe5
	.uleb128 0x47
	.4byte	.LASF471
	.byte	0x5
	.2byte	0x117
	.4byte	.L968
	.uleb128 0x23
	.4byte	.LVL800
	.4byte	0x2096
	.uleb128 0x23
	.4byte	.LVL805
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL817
	.4byte	0x453b
	.uleb128 0x23
	.4byte	.LVL824
	.4byte	0x3149
	.uleb128 0x23
	.4byte	.LVL826
	.4byte	0x21f0
	.uleb128 0x23
	.4byte	.LVL828
	.4byte	0x420f
	.uleb128 0x23
	.4byte	.LVL829
	.4byte	0x2096
	.byte	0
	.uleb128 0x48
	.4byte	.LASF409
	.byte	0x5
	.byte	0xc9
	.4byte	.LFB259
	.4byte	.LFE259-.LFB259
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x20c7
	.uleb128 0x23
	.4byte	.LVL795
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL796
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL797
	.4byte	0x2312
	.byte	0
	.uleb128 0x21
	.4byte	.LASF403
	.byte	0x5
	.byte	0x6f
	.4byte	0x62
	.4byte	.LFB258
	.4byte	.LFE258-.LFB258
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x21db
	.uleb128 0x22
	.4byte	.LASF404
	.byte	0x5
	.byte	0x6f
	.4byte	0x62
	.4byte	.LLST240
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x71
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF405
	.byte	0x5
	.byte	0x71
	.4byte	0x2ff
	.uleb128 0x2c
	.4byte	.LASF406
	.byte	0x5
	.byte	0x72
	.4byte	0x30a
	.4byte	.LLST241
	.uleb128 0x2c
	.4byte	.LASF407
	.byte	0x5
	.byte	0x73
	.4byte	0x30a
	.4byte	.LLST242
	.uleb128 0x34
	.4byte	.LASF396
	.byte	0x5
	.byte	0x74
	.4byte	0x12be
	.uleb128 0x2c
	.4byte	.LASF408
	.byte	0x5
	.byte	0x75
	.4byte	0x12f7
	.4byte	.LLST243
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x21eb
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4628
	.uleb128 0x23
	.4byte	.LVL1241
	.4byte	0x1f7b
	.uleb128 0x23
	.4byte	.LVL1243
	.4byte	0x3736
	.uleb128 0x23
	.4byte	.LVL1244
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1245
	.4byte	0x493b
	.uleb128 0x23
	.4byte	.LVL1249
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1250
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1252
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1254
	.4byte	0x41ea
	.uleb128 0x23
	.4byte	.LVL1255
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1256
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1258
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1259
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1260
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1261
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1262
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1264
	.4byte	0x420f
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x21eb
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x12
	.byte	0
	.uleb128 0x3
	.4byte	0x21db
	.uleb128 0x48
	.4byte	.LASF410
	.byte	0x5
	.byte	0x54
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2258
	.uleb128 0x22
	.4byte	.LASF411
	.byte	0x5
	.byte	0x54
	.4byte	0x30a
	.4byte	.LLST71
	.uleb128 0x22
	.4byte	.LASF181
	.byte	0x5
	.byte	0x54
	.4byte	0x30a
	.4byte	.LLST72
	.uleb128 0x2b
	.ascii	"lpa\000"
	.byte	0x5
	.byte	0x54
	.4byte	0x30a
	.4byte	.LLST73
	.uleb128 0x2c
	.4byte	.LASF412
	.byte	0x5
	.byte	0x56
	.4byte	0x2ff
	.4byte	.LLST74
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x5
	.byte	0x57
	.4byte	0x2ff
	.4byte	.LLST75
	.uleb128 0x23
	.4byte	.LVL321
	.4byte	0x4507
	.byte	0
	.uleb128 0x21
	.4byte	.LASF413
	.byte	0x5
	.byte	0x49
	.4byte	0x30a
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x228a
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x5
	.byte	0x49
	.4byte	0x2ff
	.4byte	.LLST70
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x4b
	.4byte	0x2ff
	.byte	0
	.uleb128 0x48
	.4byte	.LASF414
	.byte	0x5
	.byte	0x38
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x22ce
	.uleb128 0x4a
	.ascii	"req\000"
	.byte	0x5
	.byte	0x38
	.4byte	0xf3e
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x4b
	.4byte	.LASF415
	.byte	0x5
	.byte	0x38
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x3a
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF416
	.byte	0x5
	.byte	0x3b
	.4byte	0x2ff
	.byte	0
	.uleb128 0x48
	.4byte	.LASF417
	.byte	0x5
	.byte	0x29
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2312
	.uleb128 0x4a
	.ascii	"req\000"
	.byte	0x5
	.byte	0x29
	.4byte	0xf3e
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x4b
	.4byte	.LASF415
	.byte	0x5
	.byte	0x29
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x2b
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF416
	.byte	0x5
	.byte	0x2c
	.4byte	0x2ff
	.byte	0
	.uleb128 0x48
	.4byte	.LASF418
	.byte	0x5
	.byte	0x15
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2331
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x17
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF419
	.byte	0x2
	.2byte	0xac3
	.4byte	0x62
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2392
	.uleb128 0x43
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xac3
	.4byte	0x2ff
	.4byte	.LLST187
	.uleb128 0x3e
	.ascii	"ret\000"
	.byte	0x2
	.2byte	0xac5
	.4byte	0x62
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x23a2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4558
	.uleb128 0x23
	.4byte	.LVL907
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL908
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL909
	.4byte	0x23a7
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x23a2
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x13
	.byte	0
	.uleb128 0x3
	.4byte	0x2392
	.uleb128 0x3b
	.4byte	.LASF420
	.byte	0x2
	.2byte	0xaae
	.4byte	0x62
	.byte	0x1
	.4byte	0x23d2
	.uleb128 0x3c
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xaae
	.4byte	0x2ff
	.uleb128 0x40
	.4byte	.LASF362
	.4byte	0x23e2
	.4byte	.LASF420
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x23e2
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xf
	.byte	0
	.uleb128 0x3
	.4byte	0x23d2
	.uleb128 0x3a
	.4byte	.LASF421
	.byte	0x2
	.2byte	0xa8e
	.4byte	0x30a
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2463
	.uleb128 0x43
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa8e
	.4byte	0x12be
	.4byte	.LLST68
	.uleb128 0x44
	.4byte	.LASF378
	.byte	0x2
	.2byte	0xa90
	.4byte	0x2ff
	.4byte	.LLST69
	.uleb128 0x1e
	.4byte	.LASF423
	.byte	0x2
	.2byte	0xa91
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x21eb
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4538
	.uleb128 0x23
	.4byte	.LVL306
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL307
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL308
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL311
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF424
	.byte	0x2
	.2byte	0xa76
	.4byte	0x62
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x24f3
	.uleb128 0x43
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa76
	.4byte	0x12be
	.4byte	.LLST226
	.uleb128 0x1e
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa78
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x2503
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4531
	.uleb128 0x23
	.4byte	.LVL1176
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1177
	.4byte	0x41ea
	.uleb128 0x23
	.4byte	.LVL1178
	.4byte	0x23a7
	.uleb128 0x23
	.4byte	.LVL1179
	.4byte	0x2508
	.uleb128 0x23
	.4byte	.LVL1180
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1181
	.4byte	0x3736
	.uleb128 0x23
	.4byte	.LVL1182
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1183
	.4byte	0x420f
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x2503
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1c
	.byte	0
	.uleb128 0x3
	.4byte	0x24f3
	.uleb128 0x4c
	.4byte	.LASF425
	.byte	0x2
	.2byte	0xa12
	.4byte	0x62
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2662
	.uleb128 0x4d
	.ascii	"p\000"
	.byte	0x2
	.2byte	0xa12
	.4byte	0x12be
	.4byte	.LLST179
	.uleb128 0x44
	.4byte	.LASF426
	.byte	0x2
	.2byte	0xa14
	.4byte	0x2ff
	.4byte	.LLST180
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF357
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2ff
	.4byte	.LLST181
	.uleb128 0x3e
	.ascii	"n\000"
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x2
	.2byte	0xa15
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF427
	.byte	0x2
	.2byte	0xa16
	.4byte	0x62
	.4byte	.LLST182
	.uleb128 0x44
	.4byte	.LASF428
	.byte	0x2
	.2byte	0xa17
	.4byte	0x2ff
	.4byte	.LLST183
	.uleb128 0x3f
	.4byte	.LASF429
	.byte	0x2
	.2byte	0xa18
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x2672
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4510
	.uleb128 0x4e
	.4byte	0x3416
	.4byte	.LBB451
	.4byte	.Ldebug_ranges0+0x1c0
	.byte	0x2
	.2byte	0xa48
	.4byte	0x25d1
	.uleb128 0x2e
	.4byte	0x342f
	.uleb128 0x2e
	.4byte	0x3423
	.uleb128 0x23
	.4byte	.LVL878
	.4byte	0x5c1e
	.byte	0
	.uleb128 0x23
	.4byte	.LVL854
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL860
	.4byte	0x4026
	.uleb128 0x23
	.4byte	.LVL861
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL862
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL863
	.4byte	0x41ea
	.uleb128 0x23
	.4byte	.LVL864
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL865
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL867
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL868
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL869
	.4byte	0x3416
	.uleb128 0x23
	.4byte	.LVL871
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL873
	.4byte	0x27b7
	.uleb128 0x23
	.4byte	.LVL874
	.4byte	0x493b
	.uleb128 0x23
	.4byte	.LVL875
	.4byte	0x41ea
	.uleb128 0x23
	.4byte	.LVL880
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL883
	.4byte	0x67ee
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x2672
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x18
	.byte	0
	.uleb128 0x3
	.4byte	0x2662
	.uleb128 0x3b
	.4byte	.LASF430
	.byte	0x2
	.2byte	0xa09
	.4byte	0x62
	.byte	0x1
	.4byte	0x2695
	.uleb128 0x3c
	.4byte	.LASF405
	.byte	0x2
	.2byte	0xa09
	.4byte	0x2ff
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF431
	.byte	0x2
	.2byte	0x9c0
	.4byte	0x2ff
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x273f
	.uleb128 0x1e
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x9c2
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x3d
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x9c3
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x9c4
	.4byte	0x2ff
	.4byte	.LLST62
	.uleb128 0x44
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x9c5
	.4byte	0x120e
	.4byte	.LLST63
	.uleb128 0x44
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x9c6
	.4byte	0x2ff
	.4byte	.LLST64
	.uleb128 0x44
	.4byte	.LASF437
	.byte	0x2
	.2byte	0x9c7
	.4byte	0x2ff
	.4byte	.LLST65
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x9c8
	.4byte	0x30a
	.uleb128 0x44
	.4byte	.LASF114
	.byte	0x2
	.2byte	0x9c9
	.4byte	0x2ff
	.4byte	.LLST66
	.uleb128 0x23
	.4byte	.LVL283
	.4byte	0x273f
	.uleb128 0x23
	.4byte	.LVL290
	.4byte	0x278e
	.uleb128 0x23
	.4byte	.LVL296
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF438
	.byte	0x2
	.2byte	0x9ab
	.4byte	0x2ff
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x278e
	.uleb128 0x43
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x9ab
	.4byte	0x2ff
	.4byte	.LLST57
	.uleb128 0x1e
	.4byte	.LASF439
	.byte	0x2
	.2byte	0x9ad
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x3d
	.4byte	.LASF392
	.byte	0x2
	.2byte	0x9ae
	.4byte	0x2ff
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x9af
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF440
	.byte	0x2
	.2byte	0x9a2
	.4byte	0x2ff
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x27b7
	.uleb128 0x1e
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x9a4
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF441
	.byte	0x2
	.2byte	0x992
	.4byte	0x62
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2818
	.uleb128 0x43
	.4byte	.LASF405
	.byte	0x2
	.2byte	0x992
	.4byte	0x2ff
	.4byte	.LLST56
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x994
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x994
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF165
	.byte	0x2
	.2byte	0x995
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL192
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL193
	.4byte	0x4a9e
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF442
	.byte	0x2
	.2byte	0x933
	.4byte	0x62
	.byte	0x1
	.4byte	0x2842
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x935
	.4byte	0x62
	.uleb128 0x28
	.uleb128 0x3d
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x95a
	.4byte	0x12be
	.byte	0
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x914
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x28ad
	.uleb128 0x50
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x916
	.4byte	0x62
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x50
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x917
	.4byte	0x30a
	.uleb128 0x2
	.byte	0x91
	.sleb128 -36
	.uleb128 0x3d
	.4byte	.LASF444
	.byte	0x2
	.2byte	0x918
	.4byte	0x1d42
	.uleb128 0x23
	.4byte	.LVL654
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL657
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL658
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL659
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL660
	.4byte	0x67ee
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF445
	.byte	0x2
	.2byte	0x8e0
	.4byte	.LFB240
	.4byte	.LFE240-.LFB240
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x29c6
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8e2
	.4byte	0x2ff
	.uleb128 0x51
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x8e3
	.4byte	0x30a
	.4byte	.LLST144
	.uleb128 0x1e
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x8e4
	.4byte	0x30a
	.uleb128 0x2
	.byte	0x91
	.sleb128 -48
	.uleb128 0x3e
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x8e5
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x8e6
	.4byte	0x2ff
	.4byte	.LLST145
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x29d6
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4425
	.uleb128 0x52
	.4byte	.LBB412
	.4byte	.LBE412-.LBB412
	.4byte	0x293e
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x903
	.4byte	0x2ff
	.4byte	.LLST146
	.uleb128 0x23
	.4byte	.LVL673
	.4byte	0x67ee
	.byte	0
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB409
	.4byte	.LBE409-.LBB409
	.byte	0x2
	.2byte	0x8e9
	.4byte	0x2998
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB410
	.4byte	.LBE410-.LBB410
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB411
	.4byte	.LBE411-.LBB411
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL669
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL668
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL674
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL676
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL677
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL679
	.4byte	0x67ee
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x29d6
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xd
	.byte	0
	.uleb128 0x3
	.4byte	0x29c6
	.uleb128 0x4f
	.4byte	.LASF448
	.byte	0x2
	.2byte	0x8c1
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2a82
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8c3
	.4byte	0x2ff
	.4byte	.LLST224
	.uleb128 0x51
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x8c4
	.4byte	0x30a
	.4byte	.LLST225
	.uleb128 0x1e
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x8c5
	.4byte	0x30a
	.uleb128 0x2
	.byte	0x91
	.sleb128 -48
	.uleb128 0x3e
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x8c6
	.4byte	0x2ff
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x2a92
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4409
	.uleb128 0x23
	.4byte	.LVL1162
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1163
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1167
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1168
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1170
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1171
	.4byte	0x23a7
	.uleb128 0x23
	.4byte	.LVL1172
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1173
	.4byte	0x3593
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x2a92
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x14
	.byte	0
	.uleb128 0x3
	.4byte	0x2a82
	.uleb128 0x4f
	.4byte	.LASF449
	.byte	0x2
	.2byte	0x89c
	.4byte	.LFB238
	.4byte	.LFE238-.LFB238
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b0c
	.uleb128 0x43
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x89c
	.4byte	0x12be
	.4byte	.LLST206
	.uleb128 0x1e
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x89e
	.4byte	0x32b
	.uleb128 0x2
	.byte	0x91
	.sleb128 -56
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x89f
	.4byte	0x1d42
	.uleb128 0x3d
	.4byte	.LASF451
	.byte	0x2
	.2byte	0x8a0
	.4byte	0x62
	.uleb128 0x3d
	.4byte	.LASF174
	.byte	0x2
	.2byte	0x8a1
	.4byte	0x30a
	.uleb128 0x23
	.4byte	.LVL1048
	.4byte	0x23e7
	.uleb128 0x23
	.4byte	.LVL1049
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1050
	.4byte	0x2331
	.byte	0
	.uleb128 0x54
	.4byte	.LASF452
	.byte	0x2
	.2byte	0x882
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b86
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB510
	.4byte	.LBE510-.LBB510
	.byte	0x2
	.2byte	0x886
	.4byte	0x2b7c
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB511
	.4byte	.LBE511-.LBB511
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB512
	.4byte	.LBE512-.LBB512
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL1145
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1144
	.4byte	0x3783
	.byte	0
	.uleb128 0x45
	.4byte	.LASF454
	.byte	0x2
	.2byte	0x86e
	.byte	0x1
	.uleb128 0x4c
	.4byte	.LASF455
	.byte	0x2
	.2byte	0x843
	.4byte	0x62
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2c43
	.uleb128 0x44
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x845
	.4byte	0x2ff
	.4byte	.LLST160
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x845
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x845
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF165
	.byte	0x2
	.2byte	0x845
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF456
	.byte	0x2
	.2byte	0x846
	.4byte	0x2ff
	.4byte	.LLST161
	.uleb128 0x44
	.4byte	.LASF457
	.byte	0x2
	.2byte	0x846
	.4byte	0x2ff
	.4byte	.LLST162
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x2c53
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4384
	.uleb128 0x23
	.4byte	.LVL781
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL783
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL784
	.4byte	0x4a9e
	.uleb128 0x23
	.4byte	.LVL788
	.4byte	0x420f
	.uleb128 0x23
	.4byte	.LVL791
	.4byte	0x41ea
	.uleb128 0x23
	.4byte	.LVL793
	.4byte	0x67ee
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x2c53
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x11
	.byte	0
	.uleb128 0x3
	.4byte	0x2c43
	.uleb128 0x3a
	.4byte	.LASF458
	.byte	0x2
	.2byte	0x82d
	.4byte	0x62
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2cc3
	.uleb128 0x4d
	.ascii	"p\000"
	.byte	0x2
	.2byte	0x82d
	.4byte	0x12be
	.4byte	.LLST55
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x82f
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x82f
	.4byte	0x2ff
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x23e2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4363
	.uleb128 0x23
	.4byte	.LVL186
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL187
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL188
	.4byte	0x4a9e
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF459
	.byte	0x2
	.2byte	0x820
	.4byte	0x62
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2d14
	.uleb128 0x23
	.4byte	.LVL1268
	.4byte	0x2d3a
	.uleb128 0x23
	.4byte	.LVL1269
	.4byte	0x2d14
	.uleb128 0x23
	.4byte	.LVL1270
	.4byte	0x2d3a
	.uleb128 0x23
	.4byte	.LVL1271
	.4byte	0x2d14
	.uleb128 0x23
	.4byte	.LVL1272
	.4byte	0x1f72
	.uleb128 0x23
	.4byte	.LVL1273
	.4byte	0x2331
	.byte	0
	.uleb128 0x55
	.4byte	.LASF495
	.byte	0x2
	.2byte	0x80d
	.byte	0x1
	.4byte	0x2d3a
	.uleb128 0x3c
	.4byte	.LASF422
	.byte	0x2
	.2byte	0x80d
	.4byte	0x12be
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x80f
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF460
	.byte	0x2
	.2byte	0x6a8
	.4byte	0x62
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x307d
	.uleb128 0x43
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x6a8
	.4byte	0x12be
	.4byte	.LLST191
	.uleb128 0x44
	.4byte	.LASF461
	.byte	0x2
	.2byte	0x6aa
	.4byte	0x2ff
	.4byte	.LLST192
	.uleb128 0x44
	.4byte	.LASF462
	.byte	0x2
	.2byte	0x6aa
	.4byte	0x2ff
	.4byte	.LLST193
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF172
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF170
	.byte	0x2
	.2byte	0x6ab
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF463
	.byte	0x2
	.2byte	0x6ac
	.4byte	0x2ff
	.4byte	.LLST194
	.uleb128 0x44
	.4byte	.LASF464
	.byte	0x2
	.2byte	0x6ac
	.4byte	0x2ff
	.4byte	.LLST195
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x2
	.2byte	0x6ad
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF465
	.byte	0x2
	.2byte	0x6ae
	.4byte	0x62
	.4byte	.LLST196
	.uleb128 0x3d
	.4byte	.LASF466
	.byte	0x2
	.2byte	0x6ae
	.4byte	0x62
	.uleb128 0x3d
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x6af
	.4byte	0x2ff
	.uleb128 0x51
	.ascii	"n\000"
	.byte	0x2
	.2byte	0x6af
	.4byte	0x2ff
	.4byte	.LLST197
	.uleb128 0x3e
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x6af
	.4byte	0x2ff
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30a
	.4byte	.LLST198
	.uleb128 0x44
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30a
	.4byte	.LLST199
	.uleb128 0x44
	.4byte	.LASF406
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30a
	.4byte	.LLST200
	.uleb128 0x44
	.4byte	.LASF398
	.byte	0x2
	.2byte	0x6b0
	.4byte	0x30a
	.4byte	.LLST201
	.uleb128 0x44
	.4byte	.LASF412
	.byte	0x2
	.2byte	0x6b1
	.4byte	0x2ff
	.4byte	.LLST202
	.uleb128 0x3d
	.4byte	.LASF467
	.byte	0x2
	.2byte	0x6b1
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF468
	.byte	0x2
	.2byte	0x6b2
	.4byte	0x30a
	.4byte	.LLST203
	.uleb128 0x3d
	.4byte	.LASF469
	.byte	0x2
	.2byte	0x6b3
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x6b4
	.4byte	0x1d42
	.uleb128 0x3d
	.4byte	.LASF470
	.byte	0x2
	.2byte	0x6b5
	.4byte	0x30a
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x308d
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4306
	.uleb128 0x47
	.4byte	.LASF472
	.byte	0x2
	.2byte	0x744
	.4byte	.L1126
	.uleb128 0x47
	.4byte	.LASF402
	.byte	0x2
	.2byte	0x74c
	.4byte	.L1138
	.uleb128 0x47
	.4byte	.LASF473
	.byte	0x2
	.2byte	0x7ff
	.4byte	.L1144
	.uleb128 0x3f
	.4byte	.LASF474
	.byte	0x2
	.2byte	0x809
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x208
	.4byte	0x2f0c
	.uleb128 0x3d
	.4byte	.LASF475
	.byte	0x2
	.2byte	0x71c
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF476
	.byte	0x2
	.2byte	0x71d
	.4byte	0x30a
	.uleb128 0x23
	.4byte	.LVL966
	.4byte	0x1af9
	.byte	0
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x220
	.4byte	0x2f7e
	.uleb128 0x44
	.4byte	.LASF477
	.byte	0x2
	.2byte	0x777
	.4byte	0x30a
	.4byte	.LLST204
	.uleb128 0x44
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x778
	.4byte	0x30a
	.4byte	.LLST205
	.uleb128 0x23
	.4byte	.LVL1004
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1007
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1013
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1015
	.4byte	0x44c4
	.uleb128 0x23
	.4byte	.LVL1019
	.4byte	0x3092
	.uleb128 0x23
	.4byte	.LVL1021
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1023
	.4byte	0x44c4
	.uleb128 0x23
	.4byte	.LVL1024
	.4byte	0x44c4
	.byte	0
	.uleb128 0x52
	.4byte	.LBB477
	.4byte	.LBE477-.LBB477
	.4byte	0x2fbf
	.uleb128 0x3d
	.4byte	.LASF479
	.byte	0x2
	.2byte	0x7b6
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x7b7
	.4byte	0x30a
	.uleb128 0x23
	.4byte	.LVL1029
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1030
	.4byte	0x44c4
	.uleb128 0x23
	.4byte	.LVL1031
	.4byte	0x392e
	.byte	0
	.uleb128 0x23
	.4byte	.LVL934
	.4byte	0x442c
	.uleb128 0x23
	.4byte	.LVL936
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL941
	.4byte	0x453b
	.uleb128 0x23
	.4byte	.LVL955
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL957
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL961
	.4byte	0x44c4
	.uleb128 0x23
	.4byte	.LVL971
	.4byte	0x36a4
	.uleb128 0x23
	.4byte	.LVL975
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL982
	.4byte	0x453b
	.uleb128 0x23
	.4byte	.LVL992
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL996
	.4byte	0x44c4
	.uleb128 0x23
	.4byte	.LVL1000
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1002
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1003
	.4byte	0x44c4
	.uleb128 0x23
	.4byte	.LVL1008
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1010
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1027
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1028
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1033
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1035
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1044
	.4byte	0x3149
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x308d
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x15
	.byte	0
	.uleb128 0x3
	.4byte	0x307d
	.uleb128 0x4f
	.4byte	.LASF480
	.byte	0x2
	.2byte	0x68c
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3149
	.uleb128 0x4d
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x68c
	.4byte	0x30a
	.4byte	.LLST138
	.uleb128 0x43
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x68c
	.4byte	0x30a
	.4byte	.LLST139
	.uleb128 0x44
	.4byte	.LASF412
	.byte	0x2
	.2byte	0x68e
	.4byte	0x2ff
	.4byte	.LLST140
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x23e2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4272
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x170
	.4byte	0x3136
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x692
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF392
	.byte	0x2
	.2byte	0x693
	.4byte	0x2ff
	.4byte	.LLST141
	.uleb128 0x44
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x694
	.4byte	0x2ff
	.4byte	.LLST142
	.uleb128 0x23
	.4byte	.LVL643
	.4byte	0x4086
	.uleb128 0x23
	.4byte	.LVL644
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL645
	.4byte	0x41ea
	.byte	0
	.uleb128 0x23
	.4byte	.LVL637
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL652
	.4byte	0x392e
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF481
	.byte	0x2
	.2byte	0x678
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x319a
	.uleb128 0x56
	.4byte	.LASF396
	.byte	0x2
	.2byte	0x678
	.4byte	0x12be
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x43
	.4byte	.LASF170
	.byte	0x2
	.2byte	0x678
	.4byte	0x2ff
	.4byte	.LLST53
	.uleb128 0x43
	.4byte	.LASF172
	.byte	0x2
	.2byte	0x678
	.4byte	0x2ff
	.4byte	.LLST54
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x67a
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF482
	.byte	0x2
	.2byte	0x66f
	.4byte	0x62
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31c7
	.uleb128 0x23
	.4byte	.LVL770
	.4byte	0x31c7
	.uleb128 0x23
	.4byte	.LVL771
	.4byte	0x3220
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x658
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31fc
	.uleb128 0x3d
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x65a
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL768
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL769
	.4byte	0x17c6
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF484
	.byte	0x2
	.2byte	0x648
	.4byte	0x62
	.4byte	.LFB226
	.4byte	.LFE226-.LFB226
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3220
	.uleb128 0x23
	.4byte	.LVL767
	.4byte	0x3220
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF485
	.byte	0x2
	.2byte	0x5ea
	.4byte	0x62
	.4byte	.LFB225
	.4byte	.LFE225-.LFB225
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3330
	.uleb128 0x43
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x5ea
	.4byte	0x3330
	.4byte	.LLST152
	.uleb128 0x44
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x5ec
	.4byte	0x320
	.4byte	.LLST153
	.uleb128 0x3d
	.4byte	.LASF85
	.byte	0x2
	.2byte	0x5ec
	.4byte	0x320
	.uleb128 0x44
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x5ed
	.4byte	0x320
	.4byte	.LLST154
	.uleb128 0x3d
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x5ee
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x5ef
	.4byte	0x537
	.4byte	.LLST155
	.uleb128 0x44
	.4byte	.LASF98
	.byte	0x2
	.2byte	0x5f0
	.4byte	0x62e
	.4byte	.LLST156
	.uleb128 0x44
	.4byte	.LASF99
	.byte	0x2
	.2byte	0x5f1
	.4byte	0x537
	.4byte	.LLST157
	.uleb128 0x44
	.4byte	.LASF95
	.byte	0x2
	.2byte	0x5f2
	.4byte	0x2ff
	.4byte	.LLST158
	.uleb128 0x44
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x5f3
	.4byte	0x2ff
	.4byte	.LLST159
	.uleb128 0x3d
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x5f4
	.4byte	0x3336
	.uleb128 0x23
	.4byte	.LVL729
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL734
	.4byte	0x442c
	.uleb128 0x23
	.4byte	.LVL741
	.4byte	0x3dce
	.uleb128 0x23
	.4byte	.LVL742
	.4byte	0x3d76
	.uleb128 0x23
	.4byte	.LVL743
	.4byte	0x3ca9
	.uleb128 0x23
	.4byte	.LVL744
	.4byte	0x3ca9
	.uleb128 0x23
	.4byte	.LVL747
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL754
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL757
	.4byte	0x1af9
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x585
	.uleb128 0x7
	.byte	0x4
	.4byte	0x7b6
	.uleb128 0x4c
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x580
	.4byte	0x62
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x33eb
	.uleb128 0x44
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x582
	.4byte	0x320
	.4byte	.LLST286
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x1af4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4209
	.uleb128 0x23
	.4byte	.LVL1551
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1552
	.4byte	0x442c
	.uleb128 0x23
	.4byte	.LVL1554
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1555
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1557
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1558
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1559
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1561
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1562
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1563
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1564
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1565
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1567
	.4byte	0x1af9
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF493
	.byte	0x2
	.2byte	0x57b
	.4byte	0x62
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3416
	.uleb128 0x43
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x57b
	.4byte	0x2ff
	.4byte	.LLST52
	.byte	0
	.uleb128 0x55
	.4byte	.LASF496
	.byte	0x2
	.2byte	0x573
	.byte	0x1
	.4byte	0x343c
	.uleb128 0x3c
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x573
	.4byte	0x2ff
	.uleb128 0x3c
	.4byte	.LASF372
	.byte	0x2
	.2byte	0x573
	.4byte	0x62
	.byte	0
	.uleb128 0x42
	.4byte	.LASF498
	.byte	0x2
	.2byte	0x566
	.4byte	0x62
	.byte	0x1
	.uleb128 0x3a
	.4byte	.LASF499
	.byte	0x2
	.2byte	0x477
	.4byte	0x62
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3593
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x2
	.2byte	0x479
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF500
	.byte	0x2
	.2byte	0x479
	.4byte	0x2ff
	.4byte	.LLST150
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x47a
	.4byte	0x2ff
	.uleb128 0x1e
	.4byte	.LASF357
	.byte	0x2
	.2byte	0x47a
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x47b
	.4byte	0x320
	.uleb128 0x3e
	.ascii	"j\000"
	.byte	0x2
	.2byte	0x47b
	.4byte	0x320
	.uleb128 0x44
	.4byte	.LASF501
	.byte	0x2
	.2byte	0x47c
	.4byte	0x3336
	.4byte	.LLST151
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x29d6
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4152
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB418
	.4byte	.LBE418-.LBB418
	.byte	0x2
	.2byte	0x484
	.4byte	0x351d
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB419
	.4byte	.LBE419-.LBB419
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB420
	.4byte	.LBE420-.LBB420
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL690
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL691
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL692
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL693
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL700
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL701
	.4byte	0x4a9e
	.uleb128 0x23
	.4byte	.LVL707
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL709
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL710
	.4byte	0x435a
	.uleb128 0x23
	.4byte	.LVL711
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL712
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL713
	.4byte	0x435a
	.uleb128 0x23
	.4byte	.LVL716
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF502
	.byte	0x2
	.2byte	0x41c
	.4byte	0x62
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3652
	.uleb128 0x1e
	.4byte	.LASF86
	.byte	0x2
	.2byte	0x41e
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x3d
	.4byte	.LASF503
	.byte	0x2
	.2byte	0x41f
	.4byte	0x3652
	.uleb128 0x44
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x420
	.4byte	0x2ff
	.4byte	.LLST223
	.uleb128 0x3f
	.4byte	.LASF505
	.byte	0x2
	.2byte	0x44d
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x1af4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4130
	.uleb128 0x23
	.4byte	.LVL1147
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1149
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1150
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1151
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1152
	.4byte	0x3712
	.uleb128 0x23
	.4byte	.LVL1153
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1154
	.4byte	0x4234
	.uleb128 0x23
	.4byte	.LVL1155
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1156
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1159
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1160
	.4byte	0x435a
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x80e
	.uleb128 0x55
	.4byte	.LASF506
	.byte	0x2
	.2byte	0x3e3
	.byte	0x1
	.4byte	0x36a4
	.uleb128 0x28
	.uleb128 0x3e
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x3e8
	.4byte	0x30a
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x3e9
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF507
	.byte	0x2
	.2byte	0x3ea
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x3eb
	.4byte	0x32b
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x2
	.2byte	0x3ec
	.4byte	0x1d42
	.byte	0
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF508
	.byte	0x2
	.2byte	0x3b1
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3712
	.uleb128 0x43
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x3b1
	.4byte	0x3330
	.4byte	.LLST147
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x3b3
	.4byte	0x30a
	.4byte	.LLST148
	.uleb128 0x3d
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x3b4
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x3b5
	.4byte	0x537
	.4byte	.LLST149
	.uleb128 0x23
	.4byte	.LVL685
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL686
	.4byte	0x3ac5
	.uleb128 0x23
	.4byte	.LVL689
	.4byte	0x17c6
	.byte	0
	.uleb128 0x55
	.4byte	.LASF509
	.byte	0x2
	.2byte	0x3a2
	.byte	0x1
	.4byte	0x3736
	.uleb128 0x3c
	.4byte	.LASF183
	.byte	0x2
	.2byte	0x3a2
	.4byte	0x30a
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x3a4
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF510
	.byte	0x2
	.2byte	0x38d
	.4byte	0x62
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3783
	.uleb128 0x43
	.4byte	.LASF511
	.byte	0x2
	.2byte	0x38d
	.4byte	0x30a
	.4byte	.LLST215
	.uleb128 0x44
	.4byte	.LASF512
	.byte	0x2
	.2byte	0x38f
	.4byte	0x30a
	.4byte	.LLST216
	.uleb128 0x23
	.4byte	.LVL1124
	.4byte	0x3859
	.uleb128 0x23
	.4byte	.LVL1125
	.4byte	0x2b86
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x367
	.4byte	0x30a
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3853
	.uleb128 0x43
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x367
	.4byte	0x30a
	.4byte	.LLST217
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x367
	.4byte	0x30a
	.4byte	.LLST218
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x367
	.4byte	0x3853
	.4byte	.LLST219
	.uleb128 0x51
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x369
	.4byte	0x2ff
	.4byte	.LLST220
	.uleb128 0x3d
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x369
	.4byte	0x2ff
	.uleb128 0x51
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x36a
	.4byte	0x30a
	.4byte	.LLST221
	.uleb128 0x1e
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x36b
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x50
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x36c
	.4byte	0x32b
	.uleb128 0x3
	.byte	0x91
	.sleb128 -128
	.uleb128 0x44
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x36d
	.4byte	0x30a
	.4byte	.LLST222
	.uleb128 0x1e
	.4byte	.LASF353
	.byte	0x2
	.2byte	0x36e
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x23
	.4byte	.LVL1137
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1138
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1140
	.4byte	0x17c6
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x2f4
	.uleb128 0x4c
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x342
	.4byte	0x30a
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x392e
	.uleb128 0x43
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x342
	.4byte	0x30a
	.4byte	.LLST211
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x342
	.4byte	0x30a
	.4byte	.LLST212
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x342
	.4byte	0x3853
	.4byte	.LLST213
	.uleb128 0x50
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x344
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x3d
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x344
	.4byte	0x2ff
	.uleb128 0x51
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x345
	.4byte	0x30a
	.4byte	.LLST214
	.uleb128 0x1e
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x346
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x50
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x347
	.4byte	0x32b
	.uleb128 0x3
	.byte	0x91
	.sleb128 -128
	.uleb128 0x1e
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x348
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x1e
	.4byte	.LASF353
	.byte	0x2
	.2byte	0x349
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x23
	.4byte	.LVL1108
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1109
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1110
	.4byte	0x3ac5
	.uleb128 0x23
	.4byte	.LVL1113
	.4byte	0x17c6
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF521
	.byte	0x2
	.2byte	0x321
	.4byte	0x62
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x39e4
	.uleb128 0x4d
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x321
	.4byte	0x30a
	.4byte	.LLST134
	.uleb128 0x4d
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x321
	.4byte	0x537
	.4byte	.LLST135
	.uleb128 0x43
	.4byte	.LASF522
	.byte	0x2
	.2byte	0x321
	.4byte	0x62
	.4byte	.LLST136
	.uleb128 0x44
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x323
	.4byte	0x2ff
	.4byte	.LLST137
	.uleb128 0x1e
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x323
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x1e
	.4byte	.LASF525
	.byte	0x2
	.2byte	0x323
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x39f4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4057
	.uleb128 0x47
	.4byte	.LASF526
	.byte	0x2
	.2byte	0x32c
	.4byte	.L737
	.uleb128 0x23
	.4byte	.LVL627
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL630
	.4byte	0x3a30
	.uleb128 0x23
	.4byte	.LVL631
	.4byte	0x3a85
	.uleb128 0x23
	.4byte	.LVL632
	.4byte	0x3e7f
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x39f4
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x8
	.byte	0
	.uleb128 0x3
	.4byte	0x39e4
	.uleb128 0x3a
	.4byte	.LASF527
	.byte	0x2
	.2byte	0x314
	.4byte	0x62
	.4byte	.LFB211
	.4byte	.LFE211-.LFB211
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a30
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x316
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL921
	.4byte	0x3658
	.uleb128 0x23
	.4byte	.LVL922
	.4byte	0x3a85
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF528
	.byte	0x2
	.2byte	0x2ec
	.4byte	0x2ff
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a85
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x2ee
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF529
	.byte	0x2
	.2byte	0x2ef
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF530
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x30a
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x308d
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4040
	.uleb128 0x23
	.4byte	.LVL166
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF531
	.byte	0x2
	.2byte	0x2e2
	.4byte	0x62
	.4byte	.LFB209
	.4byte	.LFE209-.LFB209
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ac5
	.uleb128 0x43
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x2e2
	.4byte	0x2ff
	.4byte	.LLST133
	.uleb128 0x3d
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x2e4
	.4byte	0x30a
	.uleb128 0x23
	.4byte	.LVL622
	.4byte	0x3ac5
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF532
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x62
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3bd9
	.uleb128 0x43
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x3330
	.4byte	.LLST126
	.uleb128 0x43
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x30a
	.4byte	.LLST127
	.uleb128 0x56
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x2a7
	.4byte	0x537
	.uleb128 0x1
	.byte	0x52
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x2a9
	.4byte	0x30a
	.uleb128 0x44
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x2aa
	.4byte	0x3336
	.4byte	.LLST128
	.uleb128 0x44
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x2ab
	.4byte	0x2ff
	.4byte	.LLST129
	.uleb128 0x3f
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x2ad
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x23e2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4015
	.uleb128 0x4e
	.4byte	0x17c6
	.4byte	.LBB384
	.4byte	.Ldebug_ranges0+0x158
	.byte	0x2
	.2byte	0x2c3
	.4byte	0x3ba2
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x158
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x158
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL596
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL593
	.4byte	0x3bd9
	.uleb128 0x23
	.4byte	.LVL594
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL595
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL599
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL602
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL603
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF534
	.byte	0x2
	.2byte	0x277
	.4byte	0x62
	.4byte	.LFB207
	.4byte	.LFE207-.LFB207
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3c94
	.uleb128 0x43
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x277
	.4byte	0x3330
	.4byte	.LLST122
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x279
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x279
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF535
	.byte	0x2
	.2byte	0x27a
	.4byte	0x2ff
	.4byte	.LLST123
	.uleb128 0x44
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x27b
	.4byte	0x62e
	.4byte	.LLST124
	.uleb128 0x44
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x27c
	.4byte	0x537
	.4byte	.LLST125
	.uleb128 0x3d
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x27d
	.4byte	0x3336
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x3ca4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.4001
	.uleb128 0x23
	.4byte	.LVL576
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL577
	.4byte	0x4234
	.uleb128 0x23
	.4byte	.LVL583
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL586
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL587
	.4byte	0x3ca9
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x3ca4
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x1e
	.byte	0
	.uleb128 0x3
	.4byte	0x3c94
	.uleb128 0x3a
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x249
	.4byte	0x62
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3d76
	.uleb128 0x43
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x249
	.4byte	0x3330
	.4byte	.LLST130
	.uleb128 0x1e
	.4byte	.LASF538
	.byte	0x2
	.2byte	0x24b
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x3d
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x24c
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x24d
	.4byte	0x2ff
	.4byte	.LLST131
	.uleb128 0x44
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x24e
	.4byte	0x62e
	.4byte	.LLST132
	.uleb128 0x1e
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x24f
	.4byte	0x537
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x3d
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x250
	.4byte	0x3336
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x1af4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3987
	.uleb128 0x23
	.4byte	.LVL607
	.4byte	0x3dce
	.uleb128 0x23
	.4byte	.LVL609
	.4byte	0x3d76
	.uleb128 0x23
	.4byte	.LVL611
	.4byte	0x435a
	.uleb128 0x23
	.4byte	.LVL613
	.4byte	0x3d76
	.uleb128 0x23
	.4byte	.LVL615
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL617
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL619
	.4byte	0x3ac5
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF540
	.byte	0x2
	.2byte	0x231
	.4byte	0x62
	.byte	0x1
	.4byte	0x3db9
	.uleb128 0x3c
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x231
	.4byte	0x3330
	.uleb128 0x3d
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x233
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x234
	.4byte	0x62e
	.uleb128 0x40
	.4byte	.LASF362
	.4byte	0x3dc9
	.4byte	.LASF540
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x3dc9
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x19
	.byte	0
	.uleb128 0x3
	.4byte	0x3db9
	.uleb128 0x3a
	.4byte	.LASF542
	.byte	0x2
	.2byte	0x209
	.4byte	0x62
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3e7f
	.uleb128 0x43
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x209
	.4byte	0x3330
	.4byte	.LLST117
	.uleb128 0x1e
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x20b
	.4byte	0x62e
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x44
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x20c
	.4byte	0x537
	.4byte	.LLST118
	.uleb128 0x1e
	.4byte	.LASF543
	.byte	0x2
	.2byte	0x20d
	.4byte	0x62e
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x44
	.4byte	.LASF178
	.byte	0x2
	.2byte	0x20e
	.4byte	0x2ff
	.4byte	.LLST119
	.uleb128 0x3d
	.4byte	.LASF119
	.byte	0x2
	.2byte	0x20e
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x20f
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF544
	.byte	0x2
	.2byte	0x210
	.4byte	0x2ff
	.4byte	.LLST120
	.uleb128 0x44
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x211
	.4byte	0x2ff
	.4byte	.LLST121
	.uleb128 0x23
	.4byte	.LVL558
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL571
	.4byte	0x435a
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF545
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x62
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3f26
	.uleb128 0x43
	.4byte	.LASF105
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x2ff
	.4byte	.LLST114
	.uleb128 0x43
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x1e7
	.4byte	0x2ff
	.4byte	.LLST115
	.uleb128 0x44
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x1e9
	.4byte	0x3336
	.4byte	.LLST116
	.uleb128 0x1e
	.4byte	.LASF546
	.byte	0x2
	.2byte	0x1ea
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x23e2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3943
	.uleb128 0x23
	.4byte	.LVL542
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL544
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL547
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL549
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL550
	.4byte	0x6804
	.uleb128 0x23
	.4byte	.LVL551
	.4byte	0x6804
	.uleb128 0x23
	.4byte	.LVL552
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF547
	.byte	0x2
	.2byte	0x1bf
	.4byte	0x62
	.byte	0x1
	.4byte	0x3fa5
	.uleb128 0x3c
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x1bf
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF548
	.byte	0x2
	.2byte	0x1c1
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF549
	.byte	0x2
	.2byte	0x1c2
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF550
	.byte	0x2
	.2byte	0x1c3
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF551
	.byte	0x2
	.2byte	0x1c5
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF552
	.byte	0x2
	.2byte	0x1c6
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x1c7
	.4byte	0x120e
	.uleb128 0x3d
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x1c7
	.4byte	0x120e
	.uleb128 0x40
	.4byte	.LASF362
	.4byte	0x308d
	.4byte	.LASF547
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF554
	.byte	0x2
	.2byte	0x1ab
	.4byte	0x62
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3fec
	.uleb128 0x43
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x1ab
	.4byte	0x2ff
	.4byte	.LLST43
	.uleb128 0x3d
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x1ad
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x1ae
	.4byte	0x120e
	.4byte	.LLST44
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF555
	.byte	0x2
	.2byte	0x1a6
	.4byte	0x62
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4020
	.uleb128 0x43
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x1a6
	.4byte	0x4020
	.4byte	.LLST42
	.uleb128 0x23
	.4byte	.LVL140
	.4byte	0x4026
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x120e
	.uleb128 0x3a
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x193
	.4byte	0x62
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4086
	.uleb128 0x43
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x193
	.4byte	0x4020
	.4byte	.LLST39
	.uleb128 0x43
	.4byte	.LASF93
	.byte	0x2
	.2byte	0x193
	.4byte	0x2ff
	.4byte	.LLST40
	.uleb128 0x3d
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x195
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x196
	.4byte	0x120e
	.4byte	.LLST41
	.uleb128 0x23
	.4byte	.LVL134
	.4byte	0x4086
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF558
	.byte	0x2
	.2byte	0x176
	.4byte	0x62
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x40f3
	.uleb128 0x43
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x176
	.4byte	0x4020
	.4byte	.LLST37
	.uleb128 0x43
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x176
	.4byte	0x2ff
	.4byte	.LLST38
	.uleb128 0x1e
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x178
	.4byte	0x120e
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x3d
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x178
	.4byte	0x120e
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x4103
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3902
	.uleb128 0x23
	.4byte	.LVL126
	.4byte	0x67ee
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x4103
	.uleb128 0x16
	.4byte	0x2c
	.byte	0x10
	.byte	0
	.uleb128 0x3
	.4byte	0x40f3
	.uleb128 0x3a
	.4byte	.LASF560
	.byte	0x2
	.2byte	0x145
	.4byte	0x62
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4175
	.uleb128 0x43
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x145
	.4byte	0x2ff
	.4byte	.LLST35
	.uleb128 0x1e
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x147
	.4byte	0x120e
	.uleb128 0x1
	.byte	0x5c
	.uleb128 0x1e
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x147
	.4byte	0x120e
	.uleb128 0x1
	.byte	0x53
	.uleb128 0x1e
	.4byte	.LASF561
	.byte	0x2
	.2byte	0x148
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x3d
	.4byte	.LASF562
	.byte	0x2
	.2byte	0x149
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF563
	.byte	0x2
	.2byte	0x14a
	.4byte	0x2ff
	.byte	0
	.uleb128 0x26
	.4byte	.LASF564
	.byte	0x2
	.byte	0xf8
	.4byte	0x62
	.byte	0x1
	.4byte	0x41ea
	.uleb128 0x33
	.4byte	.LASF432
	.byte	0x2
	.byte	0xf8
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF435
	.byte	0x2
	.byte	0xfa
	.4byte	0x120e
	.uleb128 0x34
	.4byte	.LASF559
	.byte	0x2
	.byte	0xfa
	.4byte	0x120e
	.uleb128 0x34
	.4byte	.LASF565
	.byte	0x2
	.byte	0xfb
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF566
	.byte	0x2
	.byte	0xfc
	.4byte	0x2ff
	.uleb128 0x34
	.4byte	.LASF550
	.byte	0x2
	.byte	0xfd
	.4byte	0x30a
	.uleb128 0x34
	.4byte	.LASF567
	.byte	0x2
	.byte	0xfe
	.4byte	0x30a
	.uleb128 0x34
	.4byte	.LASF563
	.byte	0x2
	.byte	0xff
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF114
	.byte	0x2
	.2byte	0x100
	.4byte	0x2ff
	.byte	0
	.uleb128 0x57
	.4byte	.LASF568
	.byte	0x2
	.byte	0xd9
	.byte	0x1
	.4byte	0x420f
	.uleb128 0x33
	.4byte	.LASF432
	.byte	0x2
	.byte	0xd9
	.4byte	0x2ff
	.uleb128 0x40
	.4byte	.LASF362
	.4byte	0x4103
	.4byte	.LASF568
	.byte	0
	.uleb128 0x57
	.4byte	.LASF569
	.byte	0x2
	.byte	0xd2
	.byte	0x1
	.4byte	0x4234
	.uleb128 0x33
	.4byte	.LASF432
	.byte	0x2
	.byte	0xd2
	.4byte	0x2ff
	.uleb128 0x40
	.4byte	.LASF362
	.4byte	0x4103
	.4byte	.LASF569
	.byte	0
	.uleb128 0x21
	.4byte	.LASF570
	.byte	0x2
	.byte	0xb4
	.4byte	0x2ff
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x42b7
	.uleb128 0x36
	.ascii	"tmp\000"
	.byte	0x2
	.byte	0xb6
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0xb7
	.4byte	0x42b7
	.uleb128 0x58
	.4byte	.LASF571
	.byte	0x2
	.byte	0xb9
	.4byte	.L512
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0xd8
	.4byte	0x4295
	.uleb128 0x34
	.4byte	.LASF572
	.byte	0x2
	.byte	0xbf
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL462
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL463
	.4byte	0x1954
	.byte	0
	.uleb128 0x59
	.4byte	0x43a1
	.4byte	.LBB313
	.4byte	.Ldebug_ranges0+0xc0
	.byte	0x2
	.byte	0xba
	.uleb128 0x23
	.4byte	.LVL464
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL465
	.4byte	0x67ee
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0xada
	.uleb128 0x21
	.4byte	.LASF573
	.byte	0x2
	.byte	0xa2
	.4byte	0x62
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4316
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x2
	.byte	0xa2
	.4byte	0x2ff
	.4byte	.LLST25
	.uleb128 0x36
	.ascii	"ret\000"
	.byte	0x2
	.byte	0xa4
	.4byte	0x62
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x36
	.ascii	"i\000"
	.byte	0x2
	.byte	0xa5
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0xa6
	.4byte	0x42b7
	.uleb128 0x59
	.4byte	0x4395
	.4byte	.LBB253
	.4byte	.Ldebug_ranges0+0x58
	.byte	0x2
	.byte	0xa8
	.byte	0
	.uleb128 0x48
	.4byte	.LASF574
	.byte	0x2
	.byte	0x75
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x435a
	.uleb128 0x1f
	.4byte	.LASF114
	.byte	0x2
	.byte	0x77
	.4byte	0x315
	.uleb128 0x1
	.byte	0x5c
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x2
	.byte	0x78
	.4byte	0x2ff
	.uleb128 0x49
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x78
	.4byte	0x2ff
	.4byte	.LLST24
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x79
	.4byte	0x42b7
	.byte	0
	.uleb128 0x57
	.4byte	.LASF575
	.byte	0x2
	.byte	0x61
	.byte	0x1
	.4byte	0x4395
	.uleb128 0x27
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x61
	.4byte	0x2ff
	.uleb128 0x33
	.4byte	.LASF576
	.byte	0x2
	.byte	0x61
	.4byte	0x2ff
	.uleb128 0x28
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x66
	.4byte	0x42b7
	.uleb128 0x28
	.uleb128 0x34
	.4byte	.LASF572
	.byte	0x2
	.byte	0x68
	.4byte	0x2ff
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5a
	.4byte	.LASF577
	.byte	0x2
	.byte	0x5c
	.4byte	0x2ff
	.byte	0x1
	.uleb128 0x5a
	.4byte	.LASF578
	.byte	0x2
	.byte	0x57
	.4byte	0x2ff
	.byte	0x1
	.uleb128 0x21
	.4byte	.LASF579
	.byte	0x2
	.byte	0x4b
	.4byte	0x2ff
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x442c
	.uleb128 0x2b
	.ascii	"max\000"
	.byte	0x2
	.byte	0x4b
	.4byte	0x2ff
	.4byte	.LLST23
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x4d
	.4byte	0x42b7
	.uleb128 0x5b
	.4byte	0x17c6
	.4byte	.LBB244
	.4byte	.Ldebug_ranges0+0x38
	.byte	0x2
	.byte	0x53
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x38
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x38
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x23
	.4byte	.LVL71
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x21
	.4byte	.LASF580
	.byte	0x2
	.byte	0x2e
	.4byte	0x62
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x44c4
	.uleb128 0x22
	.4byte	.LASF178
	.byte	0x2
	.byte	0x2e
	.4byte	0x30a
	.4byte	.LLST84
	.uleb128 0x22
	.4byte	.LASF356
	.byte	0x2
	.byte	0x2e
	.4byte	0x62
	.4byte	.LLST85
	.uleb128 0x36
	.ascii	"req\000"
	.byte	0x2
	.byte	0x30
	.4byte	0x32b
	.uleb128 0x3
	.byte	0x91
	.sleb128 -120
	.uleb128 0x36
	.ascii	"low\000"
	.byte	0x2
	.byte	0x31
	.4byte	0x320
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x1f
	.4byte	.LASF581
	.byte	0x2
	.byte	0x31
	.4byte	0x320
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"mid\000"
	.byte	0x2
	.byte	0x31
	.4byte	0x320
	.uleb128 0x1f
	.4byte	.LASF582
	.byte	0x2
	.byte	0x32
	.4byte	0x1944
	.uleb128 0x3
	.byte	0x91
	.sleb128 -100
	.uleb128 0x34
	.4byte	.LASF583
	.byte	0x2
	.byte	0x33
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL367
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL370
	.4byte	0x1af9
	.byte	0
	.uleb128 0x21
	.4byte	.LASF584
	.byte	0x2
	.byte	0x1e
	.4byte	0x30a
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4507
	.uleb128 0x22
	.4byte	.LASF585
	.byte	0x2
	.byte	0x1e
	.4byte	0x30a
	.4byte	.LLST22
	.uleb128 0x4b
	.4byte	.LASF586
	.byte	0x2
	.byte	0x1e
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x36
	.ascii	"ret\000"
	.byte	0x2
	.byte	0x20
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF587
	.byte	0x2
	.byte	0x14
	.4byte	0x2ff
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x453b
	.uleb128 0x22
	.4byte	.LASF378
	.byte	0x2
	.byte	0x14
	.4byte	0x2ff
	.4byte	.LLST21
	.uleb128 0x34
	.4byte	.LASF500
	.byte	0x2
	.byte	0x16
	.4byte	0x2ff
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF588
	.byte	0x2
	.byte	0xc
	.4byte	0x2ff
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x456f
	.uleb128 0x22
	.4byte	.LASF378
	.byte	0x2
	.byte	0xc
	.4byte	0x2ff
	.4byte	.LLST20
	.uleb128 0x34
	.4byte	.LASF376
	.byte	0x2
	.byte	0xe
	.4byte	0x2ff
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF589
	.byte	0x2
	.byte	0x4
	.4byte	0x2ff
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x45b2
	.uleb128 0x22
	.4byte	.LASF376
	.byte	0x2
	.byte	0x4
	.4byte	0x2ff
	.4byte	.LLST18
	.uleb128 0x22
	.4byte	.LASF500
	.byte	0x2
	.byte	0x4
	.4byte	0x2ff
	.4byte	.LLST19
	.uleb128 0x34
	.4byte	.LASF392
	.byte	0x2
	.byte	0x6
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF590
	.byte	0x4
	.2byte	0x13c
	.4byte	0x315
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4774
	.uleb128 0x50
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x13e
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x3e
	.ascii	"blk\000"
	.byte	0x4
	.2byte	0x13f
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF352
	.byte	0x4
	.2byte	0x140
	.4byte	0x30a
	.4byte	.LLST210
	.uleb128 0x3d
	.4byte	.LASF591
	.byte	0x4
	.2byte	0x142
	.4byte	0x4774
	.uleb128 0x3f
	.4byte	.LASF592
	.byte	0x4
	.2byte	0x158
	.uleb128 0x4e
	.4byte	0x17c6
	.4byte	.LBB488
	.4byte	.Ldebug_ranges0+0x240
	.byte	0x4
	.2byte	0x15e
	.4byte	0x465c
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x240
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x240
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL1085
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB494
	.4byte	.LBE494-.LBB494
	.byte	0x4
	.2byte	0x15f
	.4byte	0x46b6
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB495
	.4byte	.LBE495-.LBB495
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB496
	.4byte	.LBE496-.LBB496
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL1086
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1072
	.4byte	0x48d8
	.uleb128 0x23
	.4byte	.LVL1073
	.4byte	0x477a
	.uleb128 0x23
	.4byte	.LVL1074
	.4byte	0x4a9e
	.uleb128 0x23
	.4byte	.LVL1075
	.4byte	0x1900
	.uleb128 0x23
	.4byte	.LVL1076
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1078
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1079
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1081
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1082
	.4byte	0x180a
	.uleb128 0x23
	.4byte	.LVL1083
	.4byte	0x4a38
	.uleb128 0x23
	.4byte	.LVL1084
	.4byte	0x4a9e
	.uleb128 0x23
	.4byte	.LVL1088
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1089
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL1090
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1092
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1093
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1094
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL1095
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1096
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL1097
	.4byte	0x493b
	.uleb128 0x23
	.4byte	.LVL1098
	.4byte	0x493b
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x6fe
	.uleb128 0x3a
	.4byte	.LASF593
	.byte	0x4
	.2byte	0x116
	.4byte	0x62
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x47d8
	.uleb128 0x50
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x118
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x51
	.ascii	"blk\000"
	.byte	0x4
	.2byte	0x119
	.4byte	0x2ff
	.4byte	.LLST82
	.uleb128 0x44
	.4byte	.LASF594
	.byte	0x4
	.2byte	0x11a
	.4byte	0x30a
	.4byte	.LLST83
	.uleb128 0x3d
	.4byte	.LASF591
	.byte	0x4
	.2byte	0x11b
	.4byte	0x4774
	.uleb128 0x23
	.4byte	.LVL362
	.4byte	0x1af9
	.byte	0
	.uleb128 0x21
	.4byte	.LASF595
	.byte	0x4
	.byte	0xac
	.4byte	0x62
	.4byte	.LFB179
	.4byte	.LFE179-.LFB179
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4888
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x4
	.byte	0xae
	.4byte	0x30a
	.4byte	.LLST207
	.uleb128 0x49
	.ascii	"blk\000"
	.byte	0x4
	.byte	0xaf
	.4byte	0x2ff
	.4byte	.LLST208
	.uleb128 0x2c
	.4byte	.LASF488
	.byte	0x4
	.byte	0xb0
	.4byte	0x320
	.4byte	.LLST209
	.uleb128 0x34
	.4byte	.LASF591
	.byte	0x4
	.byte	0xb1
	.4byte	0x4774
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x4898
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3749
	.uleb128 0x23
	.4byte	.LVL1051
	.4byte	0x48d8
	.uleb128 0x23
	.4byte	.LVL1053
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1054
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1055
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1056
	.4byte	0x442c
	.uleb128 0x23
	.4byte	.LVL1058
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1059
	.4byte	0x5862
	.uleb128 0x23
	.4byte	.LVL1065
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1069
	.4byte	0x176f
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x4898
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xa
	.byte	0
	.uleb128 0x3
	.4byte	0x4888
	.uleb128 0x21
	.4byte	.LASF596
	.byte	0x4
	.byte	0x9c
	.4byte	0x2ff
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x48d8
	.uleb128 0x29
	.ascii	"blk\000"
	.byte	0x4
	.byte	0x9e
	.4byte	0x2ff
	.uleb128 0x1f
	.4byte	.LASF597
	.byte	0x4
	.byte	0x9f
	.4byte	0x2ff
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x23
	.4byte	.LVL50
	.4byte	0x4a9e
	.byte	0
	.uleb128 0x48
	.4byte	.LASF598
	.byte	0x4
	.byte	0x94
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x493b
	.uleb128 0x5b
	.4byte	0x17c6
	.4byte	.LBB233
	.4byte	.Ldebug_ranges0+0x20
	.byte	0x4
	.byte	0x98
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x20
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x20
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x23
	.4byte	.LVL49
	.4byte	0x67e3
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x21
	.4byte	.LASF599
	.byte	0x4
	.byte	0x4f
	.4byte	0x62
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4a38
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x4
	.byte	0x51
	.4byte	0x62
	.4byte	.LLST175
	.uleb128 0x2c
	.4byte	.LASF600
	.byte	0x4
	.byte	0x52
	.4byte	0x62
	.4byte	.LLST176
	.uleb128 0x2c
	.4byte	.LASF591
	.byte	0x4
	.byte	0x53
	.4byte	0x4774
	.4byte	.LLST177
	.uleb128 0x2c
	.4byte	.LASF504
	.byte	0x4
	.byte	0x54
	.4byte	0x2ff
	.4byte	.LLST178
	.uleb128 0x46
	.4byte	.LASF601
	.byte	0x4
	.byte	0x67
	.uleb128 0x5c
	.4byte	.LASF362
	.4byte	0x1af4
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB442
	.4byte	.Ldebug_ranges0+0x1a0
	.byte	0x4
	.byte	0x5f
	.4byte	0x49ef
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1a0
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1a0
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL839
	.4byte	0x67e3
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL835
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL840
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL841
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL842
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL843
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL846
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL847
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL849
	.4byte	0x176f
	.byte	0
	.uleb128 0x48
	.4byte	.LASF602
	.byte	0x4
	.byte	0x41
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4a96
	.uleb128 0x22
	.4byte	.LASF536
	.byte	0x4
	.byte	0x41
	.4byte	0x62e
	.4byte	.LLST112
	.uleb128 0x22
	.4byte	.LASF603
	.byte	0x4
	.byte	0x41
	.4byte	0x537
	.4byte	.LLST113
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x43
	.4byte	0x30a
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x29d6
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3706
	.uleb128 0x23
	.4byte	.LVL537
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL538
	.4byte	0x67ee
	.byte	0
	.uleb128 0x5d
	.4byte	.LASF604
	.byte	0x4
	.byte	0x26
	.byte	0x1
	.uleb128 0x2a
	.4byte	.LASF605
	.byte	0x4
	.byte	0x17
	.4byte	0x62
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4af9
	.uleb128 0x22
	.4byte	.LASF178
	.byte	0x4
	.byte	0x17
	.4byte	0x2ff
	.4byte	.LLST15
	.uleb128 0x49
	.ascii	"die\000"
	.byte	0x4
	.byte	0x19
	.4byte	0x30a
	.4byte	.LLST16
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x1a
	.4byte	0x30a
	.uleb128 0x34
	.4byte	.LASF603
	.byte	0x4
	.byte	0x1b
	.4byte	0x537
	.uleb128 0x2c
	.4byte	.LASF369
	.byte	0x4
	.byte	0x1c
	.4byte	0x2ff
	.4byte	.LLST17
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF606
	.byte	0x4
	.byte	0x4
	.4byte	0x62
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4b4e
	.uleb128 0x22
	.4byte	.LASF178
	.byte	0x4
	.byte	0x4
	.4byte	0x2ff
	.4byte	.LLST14
	.uleb128 0x36
	.ascii	"die\000"
	.byte	0x4
	.byte	0x6
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x34
	.4byte	.LASF603
	.byte	0x4
	.byte	0x7
	.4byte	0x537
	.uleb128 0x34
	.4byte	.LASF369
	.byte	0x4
	.byte	0x8
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL40
	.4byte	0x67ee
	.byte	0
	.uleb128 0x5e
	.4byte	.LASF668
	.byte	0x3
	.2byte	0x4bd
	.4byte	0x30a
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x3a
	.4byte	.LASF607
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x62
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4bb8
	.uleb128 0x43
	.4byte	.LASF93
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x97
	.4byte	.LLST283
	.uleb128 0x43
	.4byte	.LASF114
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x97
	.4byte	.LLST284
	.uleb128 0x4d
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b8
	.4byte	0x4bb8
	.4byte	.LLST285
	.uleb128 0x23
	.4byte	.LVL1550
	.4byte	0x4ce4
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x77
	.uleb128 0x3a
	.4byte	.LASF608
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x62
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c12
	.uleb128 0x43
	.4byte	.LASF93
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x97
	.4byte	.LLST269
	.uleb128 0x43
	.4byte	.LASF114
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x97
	.4byte	.LLST270
	.uleb128 0x4d
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b3
	.4byte	0x4bb8
	.4byte	.LLST271
	.uleb128 0x23
	.4byte	.LVL1436
	.4byte	0x506e
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF609
	.byte	0x3
	.2byte	0x484
	.4byte	0x62
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4ce4
	.uleb128 0x43
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x484
	.4byte	0x30a
	.4byte	.LLST234
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x484
	.4byte	0x30a
	.4byte	.LLST235
	.uleb128 0x44
	.4byte	.LASF517
	.byte	0x3
	.2byte	0x486
	.4byte	0x2ff
	.4byte	.LLST236
	.uleb128 0x3e
	.ascii	"tmp\000"
	.byte	0x3
	.2byte	0x487
	.4byte	0x2ff
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x488
	.4byte	0x30a
	.4byte	.LLST237
	.uleb128 0x44
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x489
	.4byte	0x30a
	.4byte	.LLST238
	.uleb128 0x44
	.4byte	.LASF423
	.byte	0x3
	.2byte	0x489
	.4byte	0x30a
	.4byte	.LLST239
	.uleb128 0x3d
	.4byte	.LASF412
	.byte	0x3
	.2byte	0x48a
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL1219
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1225
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1226
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1227
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1228
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1231
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1232
	.4byte	0x2331
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF610
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x62
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f11
	.uleb128 0x4d
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x2f4
	.4byte	.LLST272
	.uleb128 0x43
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30a
	.4byte	.LLST273
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x30a
	.4byte	.LLST274
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x3853
	.4byte	.LLST275
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x1d42
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30a
	.4byte	.LLST276
	.uleb128 0x44
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30a
	.4byte	.LLST277
	.uleb128 0x44
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x30a
	.4byte	.LLST278
	.uleb128 0x44
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x30a
	.4byte	.LLST279
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x30a
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x30a
	.4byte	.LLST280
	.uleb128 0x44
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x30a
	.4byte	.LLST281
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x3c6
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x3c7
	.4byte	0x30a
	.uleb128 0x3d
	.4byte	.LASF615
	.byte	0x3
	.2byte	0x3c7
	.4byte	0x30a
	.uleb128 0x44
	.4byte	.LASF422
	.byte	0x3
	.2byte	0x3c8
	.4byte	0x12be
	.4byte	.LLST282
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x39f4
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3642
	.uleb128 0x52
	.4byte	.LBB539
	.4byte	.LBE539-.LBB539
	.4byte	0x4e2f
	.uleb128 0x1e
	.4byte	.LASF385
	.byte	0x3
	.2byte	0x43d
	.4byte	0x32b
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x23
	.4byte	.LVL1525
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1526
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1527
	.4byte	0x67ee
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1440
	.4byte	0x3859
	.uleb128 0x23
	.4byte	.LVL1452
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1461
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1463
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1464
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1465
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1468
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1472
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1478
	.4byte	0x1c75
	.uleb128 0x23
	.4byte	.LVL1480
	.4byte	0x3fa5
	.uleb128 0x23
	.4byte	.LVL1481
	.4byte	0x1e70
	.uleb128 0x23
	.4byte	.LVL1482
	.4byte	0x1c75
	.uleb128 0x23
	.4byte	.LVL1483
	.4byte	0x1c75
	.uleb128 0x23
	.4byte	.LVL1489
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1495
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1497
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1500
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1501
	.4byte	0x23e7
	.uleb128 0x23
	.4byte	.LVL1502
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1517
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1530
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1533
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1536
	.4byte	0x176f
	.uleb128 0x23
	.4byte	.LVL1543
	.4byte	0x4f35
	.uleb128 0x23
	.4byte	.LVL1545
	.4byte	0x67ee
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF616
	.byte	0x3
	.2byte	0x3b7
	.4byte	0x62
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f35
	.uleb128 0x23
	.4byte	.LVL1209
	.4byte	0x4f35
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF617
	.byte	0x3
	.2byte	0x387
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5059
	.uleb128 0x4d
	.ascii	"req\000"
	.byte	0x3
	.2byte	0x387
	.4byte	0xf3e
	.4byte	.LLST227
	.uleb128 0x43
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x387
	.4byte	0x30a
	.4byte	.LLST228
	.uleb128 0x43
	.4byte	.LASF618
	.byte	0x3
	.2byte	0x387
	.4byte	0x30a
	.4byte	.LLST229
	.uleb128 0x43
	.4byte	.LASF422
	.byte	0x3
	.2byte	0x387
	.4byte	0x12be
	.4byte	.LLST230
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x389
	.4byte	0x30a
	.4byte	.LLST231
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x38a
	.4byte	0x30a
	.4byte	.LLST232
	.uleb128 0x44
	.4byte	.LASF446
	.byte	0x3
	.2byte	0x38a
	.4byte	0x30a
	.4byte	.LLST233
	.uleb128 0x3d
	.4byte	.LASF405
	.byte	0x3
	.2byte	0x38b
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x3
	.2byte	0x38c
	.4byte	0x1d42
	.uleb128 0x3d
	.4byte	.LASF470
	.byte	0x3
	.2byte	0x38d
	.4byte	0x30a
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x5069
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3614
	.uleb128 0x23
	.4byte	.LVL1188
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1190
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1191
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1192
	.4byte	0x1e14
	.uleb128 0x23
	.4byte	.LVL1193
	.4byte	0x23e7
	.uleb128 0x23
	.4byte	.LVL1194
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL1196
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1198
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1200
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1203
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1204
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1207
	.4byte	0x67ee
	.byte	0
	.uleb128 0xb
	.4byte	0x3a
	.4byte	0x5069
	.uleb128 0x16
	.4byte	0x2c
	.byte	0xc
	.byte	0
	.uleb128 0x3
	.4byte	0x5059
	.uleb128 0x4c
	.4byte	.LASF619
	.byte	0x3
	.2byte	0x32d
	.4byte	0x62
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x524b
	.uleb128 0x4d
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x32d
	.4byte	0x2f4
	.4byte	.LLST254
	.uleb128 0x43
	.4byte	.LASF514
	.byte	0x3
	.2byte	0x32d
	.4byte	0x30a
	.4byte	.LLST255
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x32d
	.4byte	0x30a
	.4byte	.LLST256
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x32d
	.4byte	0x3853
	.4byte	.LLST257
	.uleb128 0x3e
	.ascii	"ret\000"
	.byte	0x3
	.2byte	0x32f
	.4byte	0x62
	.uleb128 0x44
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x330
	.4byte	0x30a
	.4byte	.LLST258
	.uleb128 0x44
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x330
	.4byte	0x30a
	.4byte	.LLST259
	.uleb128 0x44
	.4byte	.LASF620
	.byte	0x3
	.2byte	0x330
	.4byte	0x30a
	.4byte	.LLST260
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x331
	.4byte	0x30a
	.4byte	.LLST261
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x331
	.4byte	0x30a
	.4byte	.LLST262
	.uleb128 0x51
	.ascii	"n\000"
	.byte	0x3
	.2byte	0x333
	.4byte	0x30a
	.4byte	.LLST263
	.uleb128 0x44
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x333
	.4byte	0x30a
	.4byte	.LLST264
	.uleb128 0x44
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x333
	.4byte	0x30a
	.4byte	.LLST265
	.uleb128 0x44
	.4byte	.LASF621
	.byte	0x3
	.2byte	0x334
	.4byte	0x30a
	.4byte	.LLST266
	.uleb128 0x3d
	.4byte	.LASF622
	.byte	0x3
	.2byte	0x334
	.4byte	0x30a
	.uleb128 0x44
	.4byte	.LASF623
	.byte	0x3
	.2byte	0x335
	.4byte	0x30a
	.4byte	.LLST267
	.uleb128 0x44
	.4byte	.LASF624
	.byte	0x3
	.2byte	0x336
	.4byte	0x30a
	.4byte	.LLST268
	.uleb128 0x52
	.4byte	.LBB538
	.4byte	.LBE538-.LBB538
	.4byte	0x51ba
	.uleb128 0x3d
	.4byte	.LASF392
	.byte	0x3
	.2byte	0x37a
	.4byte	0x2ff
	.uleb128 0x23
	.4byte	.LVL1424
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL1425
	.4byte	0x1e70
	.byte	0
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB535
	.4byte	.LBE535-.LBB535
	.byte	0x3
	.2byte	0x350
	.4byte	0x5214
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB536
	.4byte	.LBE536-.LBB536
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB537
	.4byte	.LBE537-.LBB537
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL1409
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1373
	.4byte	0x3783
	.uleb128 0x23
	.4byte	.LVL1379
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1394
	.4byte	0x1c75
	.uleb128 0x23
	.4byte	.LVL1397
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1414
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1422
	.4byte	0x176f
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF625
	.byte	0x3
	.2byte	0x325
	.4byte	0x62
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x526f
	.uleb128 0x23
	.4byte	.LVL1214
	.4byte	0x526f
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF626
	.byte	0x3
	.2byte	0x31c
	.4byte	0x62
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52ae
	.uleb128 0x23
	.4byte	.LVL1210
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1211
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1212
	.4byte	0x3736
	.uleb128 0x23
	.4byte	.LVL1213
	.4byte	0x3593
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF627
	.byte	0x3
	.2byte	0x2e6
	.4byte	0x62
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x533d
	.uleb128 0x3d
	.4byte	.LASF628
	.byte	0x3
	.2byte	0x2e8
	.4byte	0x533d
	.uleb128 0x3f
	.4byte	.LASF629
	.byte	0x3
	.2byte	0x2f3
	.uleb128 0x52
	.4byte	.LBB547
	.4byte	.LBE547-.LBB547
	.4byte	0x52fd
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x310
	.4byte	0x30a
	.uleb128 0x23
	.4byte	.LVL1634
	.4byte	0x1c75
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1627
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1628
	.4byte	0x5811
	.uleb128 0x23
	.4byte	.LVL1629
	.4byte	0x5689
	.uleb128 0x23
	.4byte	.LVL1630
	.4byte	0x558d
	.uleb128 0x23
	.4byte	.LVL1631
	.4byte	0x43ad
	.uleb128 0x23
	.4byte	.LVL1632
	.4byte	0x47d8
	.uleb128 0x23
	.4byte	.LVL1633
	.4byte	0x2818
	.byte	0
	.uleb128 0x7
	.byte	0x4
	.4byte	0x417
	.uleb128 0x4c
	.4byte	.LASF630
	.byte	0x3
	.2byte	0x26e
	.4byte	0x62
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x543c
	.uleb128 0x3d
	.4byte	.LASF500
	.byte	0x3
	.2byte	0x270
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x270
	.4byte	0x2ff
	.4byte	.LLST288
	.uleb128 0x44
	.4byte	.LASF632
	.byte	0x3
	.2byte	0x271
	.4byte	0x2ff
	.4byte	.LLST289
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x272
	.4byte	0x2ff
	.uleb128 0x47
	.4byte	.LASF633
	.byte	0x3
	.2byte	0x2bc
	.4byte	.L1756
	.uleb128 0x47
	.4byte	.LASF634
	.byte	0x3
	.2byte	0x2cc
	.4byte	.L1758
	.uleb128 0x23
	.4byte	.LVL1596
	.4byte	0x43ad
	.uleb128 0x23
	.4byte	.LVL1597
	.4byte	0x47d8
	.uleb128 0x23
	.4byte	.LVL1598
	.4byte	0x45b2
	.uleb128 0x23
	.4byte	.LVL1605
	.4byte	0x493b
	.uleb128 0x23
	.4byte	.LVL1606
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1607
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL1608
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1610
	.4byte	0x543c
	.uleb128 0x23
	.4byte	.LVL1614
	.4byte	0x5862
	.uleb128 0x23
	.4byte	.LVL1615
	.4byte	0x43ad
	.uleb128 0x23
	.4byte	.LVL1617
	.4byte	0x543c
	.uleb128 0x23
	.4byte	.LVL1619
	.4byte	0x543c
	.uleb128 0x23
	.4byte	.LVL1623
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1624
	.4byte	0x4234
	.uleb128 0x23
	.4byte	.LVL1625
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1626
	.4byte	0x2818
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF635
	.byte	0x3
	.2byte	0x20f
	.4byte	0x62
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5563
	.uleb128 0x43
	.4byte	.LASF500
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2ff
	.4byte	.LLST96
	.uleb128 0x43
	.4byte	.LASF636
	.byte	0x3
	.2byte	0x20f
	.4byte	0x2f4
	.4byte	.LLST97
	.uleb128 0x44
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x211
	.4byte	0x2ff
	.4byte	.LLST98
	.uleb128 0x3d
	.4byte	.LASF357
	.byte	0x3
	.2byte	0x211
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x3
	.2byte	0x212
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF178
	.byte	0x3
	.2byte	0x212
	.4byte	0x2ff
	.uleb128 0x44
	.4byte	.LASF637
	.byte	0x3
	.2byte	0x213
	.4byte	0x2ff
	.4byte	.LLST99
	.uleb128 0x44
	.4byte	.LASF638
	.byte	0x3
	.2byte	0x214
	.4byte	0x2ff
	.4byte	.LLST100
	.uleb128 0x44
	.4byte	.LASF639
	.byte	0x3
	.2byte	0x215
	.4byte	0x2ff
	.4byte	.LLST101
	.uleb128 0x44
	.4byte	.LASF640
	.byte	0x3
	.2byte	0x216
	.4byte	0x2ff
	.4byte	.LLST102
	.uleb128 0x47
	.4byte	.LASF641
	.byte	0x3
	.2byte	0x240
	.4byte	.L478
	.uleb128 0x23
	.4byte	.LVL419
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL430
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL439
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL441
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL442
	.4byte	0x5563
	.uleb128 0x23
	.4byte	.LVL443
	.4byte	0x4a9e
	.uleb128 0x23
	.4byte	.LVL448
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL451
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL452
	.4byte	0x5563
	.uleb128 0x23
	.4byte	.LVL453
	.4byte	0x4a9e
	.uleb128 0x23
	.4byte	.LVL458
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL461
	.4byte	0x435a
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF642
	.byte	0x3
	.2byte	0x201
	.4byte	0x62
	.byte	0x1
	.4byte	0x558d
	.uleb128 0x3c
	.4byte	.LASF178
	.byte	0x3
	.2byte	0x201
	.4byte	0x2ff
	.uleb128 0x3d
	.4byte	.LASF643
	.byte	0x3
	.2byte	0x203
	.4byte	0x2ff
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF644
	.byte	0x3
	.2byte	0x1ec
	.4byte	0x62
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5689
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB431
	.4byte	.LBE431-.LBB431
	.byte	0x3
	.2byte	0x1f9
	.4byte	0x5601
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB432
	.4byte	.LBE432-.LBB432
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB433
	.4byte	.LBE433-.LBB433
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL775
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x53
	.4byte	0x17c6
	.4byte	.LBB434
	.4byte	.LBE434-.LBB434
	.byte	0x3
	.2byte	0x1fa
	.4byte	0x565b
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB435
	.4byte	.LBE435-.LBB435
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB436
	.4byte	.LBE436-.LBB436
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL776
	.4byte	0x67f9
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL772
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL773
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL774
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL777
	.4byte	0x2312
	.uleb128 0x23
	.4byte	.LVL778
	.4byte	0x31c7
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF645
	.byte	0x3
	.2byte	0x116
	.4byte	0x62
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5811
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x118
	.4byte	0x30a
	.4byte	.LLST111
	.uleb128 0x3d
	.4byte	.LASF646
	.byte	0x3
	.2byte	0x119
	.4byte	0x30a
	.uleb128 0x37
	.4byte	.LASF362
	.4byte	0x4898
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.3490
	.uleb128 0x23
	.4byte	.LVL494
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL495
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL496
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL497
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL498
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL499
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL500
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL501
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL502
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL503
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL504
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL505
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL506
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL507
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL508
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL509
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL510
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL511
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL512
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL513
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL514
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL515
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL516
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL517
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL518
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL519
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL520
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL521
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL522
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL523
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL524
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL525
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL526
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL527
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL528
	.4byte	0x680f
	.uleb128 0x23
	.4byte	.LVL531
	.4byte	0x67ee
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF647
	.byte	0x3
	.byte	0xb6
	.4byte	0x62
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5862
	.uleb128 0x22
	.4byte	.LASF628
	.byte	0x3
	.byte	0xb6
	.4byte	0x533d
	.4byte	.LLST11
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x3
	.byte	0xb8
	.4byte	0x30a
	.4byte	.LLST12
	.uleb128 0x23
	.4byte	.LVL27
	.4byte	0x58a3
	.uleb128 0x23
	.4byte	.LVL28
	.4byte	0x58a3
	.uleb128 0x23
	.4byte	.LVL32
	.4byte	0x5862
	.byte	0
	.uleb128 0x21
	.4byte	.LASF648
	.byte	0x3
	.byte	0xa4
	.4byte	0x62
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x588b
	.uleb128 0x22
	.4byte	.LASF649
	.byte	0x3
	.byte	0xa4
	.4byte	0x2ff
	.4byte	.LLST10
	.byte	0
	.uleb128 0x57
	.4byte	.LASF650
	.byte	0x3
	.byte	0x9e
	.byte	0x1
	.4byte	0x58a3
	.uleb128 0x33
	.4byte	.LASF651
	.byte	0x3
	.byte	0x9e
	.4byte	0x30a
	.byte	0
	.uleb128 0x21
	.4byte	.LASF652
	.byte	0x3
	.byte	0x7e
	.4byte	0x2ff
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x58e2
	.uleb128 0x2b
	.ascii	"num\000"
	.byte	0x3
	.byte	0x7e
	.4byte	0x30a
	.4byte	.LLST9
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x3
	.byte	0x80
	.4byte	0x2ff
	.uleb128 0x1f
	.4byte	.LASF653
	.byte	0x3
	.byte	0x81
	.4byte	0x30a
	.uleb128 0x1
	.byte	0x52
	.byte	0
	.uleb128 0x21
	.4byte	.LASF654
	.byte	0x3
	.byte	0x70
	.4byte	0x62
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5928
	.uleb128 0x22
	.4byte	.LASF85
	.byte	0x3
	.byte	0x70
	.4byte	0xb4
	.4byte	.LLST61
	.uleb128 0x29
	.ascii	"buf\000"
	.byte	0x3
	.byte	0x72
	.4byte	0xb4
	.uleb128 0x23
	.4byte	.LVL276
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL277
	.4byte	0x5928
	.byte	0
	.uleb128 0x21
	.4byte	.LASF655
	.byte	0x3
	.byte	0xa
	.4byte	0x62
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c1e
	.uleb128 0x22
	.4byte	.LASF85
	.byte	0x3
	.byte	0xa
	.4byte	0xb4
	.4byte	.LLST58
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x3
	.byte	0xc
	.4byte	0x62
	.uleb128 0x29
	.ascii	"buf\000"
	.byte	0x3
	.byte	0xd
	.4byte	0xb4
	.uleb128 0x52
	.4byte	.LBB285
	.4byte	.LBE285-.LBB285
	.4byte	0x5993
	.uleb128 0x2c
	.4byte	.LASF392
	.byte	0x3
	.byte	0x56
	.4byte	0x2ff
	.4byte	.LLST59
	.uleb128 0x23
	.4byte	.LVL267
	.4byte	0x3fa5
	.uleb128 0x23
	.4byte	.LVL269
	.4byte	0x681a
	.byte	0
	.uleb128 0x52
	.4byte	.LBB286
	.4byte	.LBE286-.LBB286
	.4byte	0x59b9
	.uleb128 0x2c
	.4byte	.LASF392
	.byte	0x3
	.byte	0x5f
	.4byte	0x2ff
	.4byte	.LLST60
	.uleb128 0x23
	.4byte	.LVL273
	.4byte	0x681a
	.byte	0
	.uleb128 0x23
	.4byte	.LVL197
	.4byte	0x6826
	.uleb128 0x23
	.4byte	.LVL198
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL199
	.4byte	0x6826
	.uleb128 0x23
	.4byte	.LVL200
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL201
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL202
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL203
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL204
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL205
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL206
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL207
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL208
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL209
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL210
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL211
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL212
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL213
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL214
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL215
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL216
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL217
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL218
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL219
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL220
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL221
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL222
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL223
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL224
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL225
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL226
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL227
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL228
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL229
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL230
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL231
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL232
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL233
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL234
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL235
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL236
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL237
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL238
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL239
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL240
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL241
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL242
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL243
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL244
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL245
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL246
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL247
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL248
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL249
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL250
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL251
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL252
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL253
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL254
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL255
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL256
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL257
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL258
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL259
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL260
	.4byte	0x278e
	.uleb128 0x23
	.4byte	.LVL261
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL262
	.4byte	0x273f
	.uleb128 0x23
	.4byte	.LVL263
	.4byte	0x681a
	.uleb128 0x23
	.4byte	.LVL266
	.4byte	0x681a
	.byte	0
	.uleb128 0x5f
	.4byte	0x3416
	.4byte	.LFB286
	.4byte	.LFE286-.LFB286
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c40
	.uleb128 0x60
	.4byte	0x3423
	.4byte	.LLST8
	.uleb128 0x2e
	.4byte	0x342f
	.byte	0
	.uleb128 0x5f
	.4byte	0x588b
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c5b
	.uleb128 0x61
	.4byte	0x5897
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x5f
	.4byte	0x5563
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c93
	.uleb128 0x60
	.4byte	0x5574
	.4byte	.LLST13
	.uleb128 0x30
	.4byte	0x5580
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x2e
	.4byte	0x5574
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x30
	.4byte	0x5c77
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x62
	.4byte	0x4a96
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x62
	.4byte	0x43a1
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x62
	.4byte	0x4395
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x5f
	.4byte	0x4175
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5d5a
	.uleb128 0x60
	.4byte	0x4185
	.4byte	.LLST26
	.uleb128 0x30
	.4byte	0x4190
	.uleb128 0x30
	.4byte	0x419b
	.uleb128 0x30
	.4byte	0x41a6
	.uleb128 0x30
	.4byte	0x41b1
	.uleb128 0x30
	.4byte	0x41bc
	.uleb128 0x30
	.4byte	0x41c7
	.uleb128 0x30
	.4byte	0x41d2
	.uleb128 0x30
	.4byte	0x41dd
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x70
	.uleb128 0x2e
	.4byte	0x4185
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x70
	.uleb128 0x63
	.4byte	0x5cdc
	.4byte	.LLST27
	.uleb128 0x63
	.4byte	0x5ce1
	.4byte	.LLST28
	.uleb128 0x63
	.4byte	0x5ce6
	.4byte	.LLST29
	.uleb128 0x63
	.4byte	0x5ceb
	.4byte	.LLST30
	.uleb128 0x63
	.4byte	0x5cf0
	.4byte	.LLST31
	.uleb128 0x63
	.4byte	0x5cf5
	.4byte	.LLST32
	.uleb128 0x30
	.4byte	0x5cfa
	.uleb128 0x63
	.4byte	0x5cff
	.4byte	.LLST33
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x41ea
	.4byte	.LFB195
	.4byte	.LFE195-.LFB195
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5db0
	.uleb128 0x60
	.4byte	0x41f6
	.4byte	.LLST34
	.uleb128 0x30
	.4byte	0x4201
	.uleb128 0x52
	.4byte	.LBB265
	.4byte	.LBE265-.LBB265
	.4byte	0x5da6
	.uleb128 0x2e
	.4byte	0x41f6
	.uleb128 0x32
	.4byte	.LBB266
	.4byte	.LBE266-.LBB266
	.uleb128 0x30
	.4byte	0x5d76
	.uleb128 0x23
	.4byte	.LVL112
	.4byte	0x67ee
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL111
	.4byte	0x4175
	.byte	0
	.uleb128 0x5f
	.4byte	0x420f
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5e06
	.uleb128 0x60
	.4byte	0x421b
	.4byte	.LLST36
	.uleb128 0x30
	.4byte	0x4226
	.uleb128 0x52
	.4byte	.LBB269
	.4byte	.LBE269-.LBB269
	.4byte	0x5dfc
	.uleb128 0x2e
	.4byte	0x421b
	.uleb128 0x32
	.4byte	.LBB270
	.4byte	.LBE270-.LBB270
	.uleb128 0x30
	.4byte	0x5dcc
	.uleb128 0x23
	.4byte	.LVL122
	.4byte	0x67ee
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL121
	.4byte	0x4108
	.byte	0
	.uleb128 0x5f
	.4byte	0x3f26
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5ec0
	.uleb128 0x60
	.4byte	0x3f37
	.4byte	.LLST45
	.uleb128 0x30
	.4byte	0x3f43
	.uleb128 0x30
	.4byte	0x3f4f
	.uleb128 0x30
	.4byte	0x3f5b
	.uleb128 0x30
	.4byte	0x3f67
	.uleb128 0x30
	.4byte	0x3f73
	.uleb128 0x30
	.4byte	0x3f7f
	.uleb128 0x30
	.4byte	0x3f8b
	.uleb128 0x30
	.4byte	0x3f97
	.uleb128 0x32
	.4byte	.LBB273
	.4byte	.LBE273-.LBB273
	.uleb128 0x2e
	.4byte	0x3f37
	.uleb128 0x32
	.4byte	.LBB274
	.4byte	.LBE274-.LBB274
	.uleb128 0x30
	.4byte	0x5e22
	.uleb128 0x63
	.4byte	0x5e27
	.4byte	.LLST46
	.uleb128 0x64
	.4byte	0x5e2c
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x63
	.4byte	0x5e31
	.4byte	.LLST47
	.uleb128 0x63
	.4byte	0x5e36
	.4byte	.LLST48
	.uleb128 0x64
	.4byte	0x5e3b
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x30
	.4byte	0x5e40
	.uleb128 0x30
	.4byte	0x5e45
	.uleb128 0x23
	.4byte	.LVL158
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL162
	.4byte	0x4086
	.uleb128 0x23
	.4byte	.LVL163
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL164
	.4byte	0x41ea
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x3712
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5ef8
	.uleb128 0x60
	.4byte	0x371f
	.4byte	.LLST49
	.uleb128 0x30
	.4byte	0x372b
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x88
	.uleb128 0x2e
	.4byte	0x371f
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x88
	.uleb128 0x30
	.4byte	0x5edc
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x3416
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f27
	.uleb128 0x60
	.4byte	0x3423
	.4byte	.LLST50
	.uleb128 0x60
	.4byte	0x342f
	.4byte	.LLST51
	.uleb128 0x23
	.4byte	.LVL170
	.4byte	0x5c1e
	.byte	0
	.uleb128 0x5f
	.4byte	0x2d14
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f67
	.uleb128 0x61
	.4byte	0x2d21
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x2d2d
	.uleb128 0x32
	.4byte	.LBB283
	.4byte	.LBE283-.LBB283
	.uleb128 0x2e
	.4byte	0x2d21
	.uleb128 0x32
	.4byte	.LBB284
	.4byte	.LBE284-.LBB284
	.uleb128 0x64
	.4byte	0x5f41
	.uleb128 0x1
	.byte	0x51
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x2677
	.4byte	.LFB247
	.4byte	.LFE247-.LFB247
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f9c
	.uleb128 0x60
	.4byte	0x2688
	.4byte	.LLST67
	.uleb128 0x32
	.4byte	.LBB289
	.4byte	.LBE289-.LBB289
	.uleb128 0x2e
	.4byte	0x2688
	.uleb128 0x23
	.4byte	.LVL303
	.4byte	0x420f
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x1e07
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fc2
	.uleb128 0x23
	.4byte	.LVL336
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL337
	.4byte	0x1e70
	.byte	0
	.uleb128 0x5f
	.4byte	0x435a
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x603b
	.uleb128 0x60
	.4byte	0x4366
	.4byte	.LLST94
	.uleb128 0x60
	.4byte	0x4371
	.4byte	.LLST95
	.uleb128 0x65
	.4byte	0x4395
	.4byte	.LBB308
	.4byte	.LBE308-.LBB308
	.byte	0x2
	.byte	0x65
	.uleb128 0x32
	.4byte	.LBB310
	.4byte	.LBE310-.LBB310
	.uleb128 0x2e
	.4byte	0x4371
	.uleb128 0x2e
	.4byte	0x4366
	.uleb128 0x32
	.4byte	.LBB311
	.4byte	.LBE311-.LBB311
	.uleb128 0x30
	.4byte	0x437d
	.uleb128 0x32
	.4byte	.LBB312
	.4byte	.LBE312-.LBB312
	.uleb128 0x30
	.4byte	0x4387
	.uleb128 0x23
	.4byte	.LVL409
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL410
	.4byte	0x1954
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x3d76
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x60b0
	.uleb128 0x60
	.4byte	0x3d87
	.4byte	.LLST103
	.uleb128 0x64
	.4byte	0x3d93
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x30
	.4byte	0x3d9f
	.uleb128 0x30
	.4byte	0x3dab
	.uleb128 0x52
	.4byte	.LBB321
	.4byte	.LBE321-.LBB321
	.4byte	0x609d
	.uleb128 0x2e
	.4byte	0x3d87
	.uleb128 0x32
	.4byte	.LBB322
	.4byte	.LBE322-.LBB322
	.uleb128 0x30
	.4byte	0x6057
	.uleb128 0x30
	.4byte	0x605e
	.uleb128 0x30
	.4byte	0x6063
	.uleb128 0x23
	.4byte	.LVL471
	.4byte	0x67ee
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL468
	.4byte	0x4234
	.uleb128 0x23
	.4byte	.LVL469
	.4byte	0x67ee
	.byte	0
	.uleb128 0x5f
	.4byte	0x1900
	.4byte	.LFB275
	.4byte	.LFE275-.LFB275
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x61c5
	.uleb128 0x60
	.4byte	0x1910
	.4byte	.LLST104
	.uleb128 0x30
	.4byte	0x191b
	.uleb128 0x30
	.4byte	0x1926
	.uleb128 0x30
	.4byte	0x1931
	.uleb128 0x32
	.4byte	.LBB335
	.4byte	.LBE335-.LBB335
	.uleb128 0x2e
	.4byte	0x1910
	.uleb128 0x32
	.4byte	.LBB336
	.4byte	.LBE336-.LBB336
	.uleb128 0x30
	.4byte	0x60cc
	.uleb128 0x64
	.4byte	0x60d1
	.uleb128 0x3
	.byte	0x91
	.sleb128 -88
	.uleb128 0x64
	.4byte	0x60d6
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x66
	.4byte	0x193c
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB337
	.4byte	.Ldebug_ranges0+0xf0
	.byte	0x1
	.byte	0xa7
	.4byte	0x615f
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xf0
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xf0
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x23
	.4byte	.LVL477
	.4byte	0x67e3
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x2d
	.4byte	0x17c6
	.4byte	.LBB343
	.4byte	.Ldebug_ranges0+0x108
	.byte	0x1
	.byte	0xa8
	.4byte	0x61a7
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x108
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x108
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL478
	.4byte	0x1954
	.uleb128 0x23
	.4byte	.LVL479
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL480
	.4byte	0x1954
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x17c6
	.4byte	.LFB277
	.4byte	.LFE277-.LFB277
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x623e
	.uleb128 0x60
	.4byte	0x17d6
	.4byte	.LLST108
	.uleb128 0x60
	.4byte	0x17df
	.4byte	.LLST109
	.uleb128 0x60
	.4byte	0x17e8
	.4byte	.LLST110
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x140
	.4byte	0x6207
	.uleb128 0x30
	.4byte	0x17f2
	.uleb128 0x30
	.4byte	0x17fd
	.byte	0
	.uleb128 0x32
	.4byte	.LBB377
	.4byte	.LBE377-.LBB377
	.uleb128 0x2e
	.4byte	0x17e8
	.uleb128 0x2e
	.4byte	0x17df
	.uleb128 0x2e
	.4byte	0x17d6
	.uleb128 0x32
	.4byte	.LBB378
	.4byte	.LBE378-.LBB378
	.uleb128 0x30
	.4byte	0x61fc
	.uleb128 0x30
	.4byte	0x6201
	.uleb128 0x23
	.4byte	.LVL493
	.4byte	0x67e3
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x1d5d
	.4byte	.LFB268
	.4byte	.LFE268-.LFB268
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x62d8
	.uleb128 0x64
	.4byte	0x1d6e
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x1d7a
	.uleb128 0x30
	.4byte	0x1d84
	.uleb128 0x30
	.4byte	0x1d90
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x188
	.4byte	0x62c8
	.uleb128 0x30
	.4byte	0x6251
	.uleb128 0x30
	.4byte	0x6258
	.uleb128 0x63
	.4byte	0x625d
	.4byte	.LLST143
	.uleb128 0x64
	.4byte	0x6262
	.uleb128 0x3
	.byte	0x91
	.sleb128 -88
	.uleb128 0x52
	.4byte	.LBB399
	.4byte	.LBE399-.LBB399
	.4byte	0x62be
	.uleb128 0x64
	.4byte	0x1da1
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x23
	.4byte	.LVL663
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL664
	.4byte	0x4507
	.uleb128 0x23
	.4byte	.LVL665
	.4byte	0x1e70
	.byte	0
	.uleb128 0x23
	.4byte	.LVL662
	.4byte	0x392e
	.byte	0
	.uleb128 0x32
	.4byte	.LBB402
	.4byte	.LBE402-.LBB402
	.uleb128 0x30
	.4byte	0x1daf
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x1dbd
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6363
	.uleb128 0x60
	.4byte	0x1dce
	.4byte	.LLST184
	.uleb128 0x30
	.4byte	0x1dda
	.uleb128 0x30
	.4byte	0x1de6
	.uleb128 0x30
	.4byte	0x1df0
	.uleb128 0x30
	.4byte	0x1dfa
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1d8
	.uleb128 0x2e
	.4byte	0x1dce
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1d8
	.uleb128 0x30
	.4byte	0x62f4
	.uleb128 0x64
	.4byte	0x62f9
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x63
	.4byte	0x62fe
	.4byte	.LLST185
	.uleb128 0x64
	.4byte	0x6303
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x23
	.4byte	.LVL890
	.4byte	0x456f
	.uleb128 0x23
	.4byte	.LVL891
	.4byte	0x1e07
	.uleb128 0x23
	.4byte	.LVL892
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL893
	.4byte	0x4af9
	.uleb128 0x23
	.4byte	.LVL894
	.4byte	0x493b
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x23a7
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x63dd
	.uleb128 0x60
	.4byte	0x23b8
	.4byte	.LLST186
	.uleb128 0x30
	.4byte	0x23c4
	.uleb128 0x52
	.4byte	.LBB463
	.4byte	.LBE463-.LBB463
	.4byte	0x63d3
	.uleb128 0x2e
	.4byte	0x23b8
	.uleb128 0x32
	.4byte	.LBB464
	.4byte	.LBE464-.LBB464
	.uleb128 0x30
	.4byte	0x637f
	.uleb128 0x23
	.4byte	.LVL899
	.4byte	0x4086
	.uleb128 0x23
	.4byte	.LVL900
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL901
	.4byte	0x2677
	.uleb128 0x23
	.4byte	.LVL902
	.4byte	0x1dbd
	.uleb128 0x23
	.4byte	.LVL903
	.4byte	0x67ee
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL904
	.4byte	0x3f26
	.byte	0
	.uleb128 0x5f
	.4byte	0x3658
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6455
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1f0
	.uleb128 0x63
	.4byte	0x3666
	.4byte	.LLST188
	.uleb128 0x63
	.4byte	0x3672
	.4byte	.LLST189
	.uleb128 0x63
	.4byte	0x367e
	.4byte	.LLST190
	.uleb128 0x64
	.4byte	0x368a
	.uleb128 0x2
	.byte	0x91
	.sleb128 -64
	.uleb128 0x30
	.4byte	0x3696
	.uleb128 0x23
	.4byte	.LVL911
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL912
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL915
	.4byte	0x17c6
	.uleb128 0x23
	.4byte	.LVL917
	.4byte	0x23e7
	.uleb128 0x23
	.4byte	.LVL918
	.4byte	0x19ea
	.uleb128 0x23
	.4byte	.LVL919
	.4byte	0x2331
	.byte	0
	.byte	0
	.uleb128 0x5f
	.4byte	0x2b86
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6472
	.uleb128 0x23
	.4byte	.LVL1114
	.4byte	0x3859
	.byte	0
	.uleb128 0x5f
	.4byte	0x343c
	.4byte	.LFB221
	.4byte	.LFE221-.LFB221
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x64a1
	.uleb128 0x23
	.4byte	.LVL1141
	.4byte	0x3783
	.uleb128 0x23
	.4byte	.LVL1142
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1143
	.4byte	0x17c6
	.byte	0
	.uleb128 0x5f
	.4byte	0x1f72
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x64d0
	.uleb128 0x23
	.4byte	.LVL1265
	.4byte	0x1f7b
	.uleb128 0x23
	.4byte	.LVL1266
	.4byte	0x36a4
	.uleb128 0x23
	.4byte	.LVL1267
	.4byte	0x20c7
	.byte	0
	.uleb128 0x5f
	.4byte	0x1c75
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x66c1
	.uleb128 0x60
	.4byte	0x1c86
	.4byte	.LLST245
	.uleb128 0x60
	.4byte	0x1c92
	.4byte	.LLST246
	.uleb128 0x30
	.4byte	0x1c9e
	.uleb128 0x30
	.4byte	0x1caa
	.uleb128 0x30
	.4byte	0x1cb6
	.uleb128 0x30
	.4byte	0x1cc2
	.uleb128 0x30
	.4byte	0x1cce
	.uleb128 0x30
	.4byte	0x1cda
	.uleb128 0x30
	.4byte	0x1ce6
	.uleb128 0x30
	.4byte	0x1cf2
	.uleb128 0x30
	.4byte	0x1cfe
	.uleb128 0x30
	.4byte	0x1d0a
	.uleb128 0x30
	.4byte	0x1d26
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x258
	.4byte	0x66b7
	.uleb128 0x2e
	.4byte	0x1c92
	.uleb128 0x2e
	.4byte	0x1c86
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x258
	.uleb128 0x63
	.4byte	0x64f5
	.4byte	.LLST247
	.uleb128 0x63
	.4byte	0x64fa
	.4byte	.LLST248
	.uleb128 0x63
	.4byte	0x64ff
	.4byte	.LLST249
	.uleb128 0x30
	.4byte	0x6504
	.uleb128 0x30
	.4byte	0x6509
	.uleb128 0x30
	.4byte	0x650e
	.uleb128 0x63
	.4byte	0x6513
	.4byte	.LLST250
	.uleb128 0x63
	.4byte	0x6518
	.4byte	.LLST251
	.uleb128 0x63
	.4byte	0x651d
	.4byte	.LLST252
	.uleb128 0x63
	.4byte	0x6522
	.4byte	.LLST253
	.uleb128 0x66
	.4byte	0x1d16
	.uleb128 0x66
	.4byte	0x1d1e
	.uleb128 0x30
	.4byte	0x6527
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x278
	.4byte	0x65dd
	.uleb128 0x30
	.4byte	0x1d34
	.uleb128 0x23
	.4byte	.LVL1292
	.4byte	0x2695
	.uleb128 0x23
	.4byte	.LVL1293
	.4byte	0x3fa5
	.uleb128 0x23
	.4byte	.LVL1294
	.4byte	0x3fa5
	.uleb128 0x23
	.4byte	.LVL1295
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1297
	.4byte	0x1e07
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1309
	.4byte	0x2258
	.uleb128 0x23
	.4byte	.LVL1310
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1316
	.4byte	0x1d5d
	.uleb128 0x23
	.4byte	.LVL1317
	.4byte	0x3fa5
	.uleb128 0x23
	.4byte	.LVL1318
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1320
	.4byte	0x2258
	.uleb128 0x23
	.4byte	.LVL1321
	.4byte	0x2331
	.uleb128 0x23
	.4byte	.LVL1322
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1323
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1324
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1325
	.4byte	0x2c58
	.uleb128 0x23
	.4byte	.LVL1327
	.4byte	0x1d5d
	.uleb128 0x23
	.4byte	.LVL1343
	.4byte	0x1af9
	.uleb128 0x23
	.4byte	.LVL1346
	.4byte	0x67ee
	.uleb128 0x23
	.4byte	.LVL1348
	.4byte	0x392e
	.uleb128 0x23
	.4byte	.LVL1349
	.4byte	0x1f18
	.uleb128 0x23
	.4byte	.LVL1352
	.4byte	0x228a
	.uleb128 0x23
	.4byte	.LVL1353
	.4byte	0x1ea4
	.uleb128 0x23
	.4byte	.LVL1360
	.4byte	0x1ea4
	.uleb128 0x23
	.4byte	.LVL1361
	.4byte	0x23a7
	.uleb128 0x23
	.4byte	.LVL1362
	.4byte	0x4f11
	.uleb128 0x23
	.4byte	.LVL1363
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1364
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1367
	.4byte	0x3fa5
	.byte	0
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1296
	.4byte	0x20c7
	.byte	0
	.uleb128 0x5f
	.4byte	0x2818
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x67cf
	.uleb128 0x30
	.4byte	0x2829
	.uleb128 0x52
	.4byte	.LBB544
	.4byte	.LBE544-.LBB544
	.4byte	0x676b
	.uleb128 0x63
	.4byte	0x66d4
	.4byte	.LLST287
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x298
	.4byte	0x66fe
	.uleb128 0x30
	.4byte	0x2834
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1582
	.4byte	0x2a97
	.uleb128 0x23
	.4byte	.LVL1583
	.4byte	0x2a97
	.uleb128 0x23
	.4byte	.LVL1584
	.4byte	0x36a4
	.uleb128 0x23
	.4byte	.LVL1585
	.4byte	0x36a4
	.uleb128 0x23
	.4byte	.LVL1586
	.4byte	0x39f9
	.uleb128 0x23
	.4byte	.LVL1587
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1588
	.4byte	0x3593
	.uleb128 0x23
	.4byte	.LVL1591
	.4byte	0x1e70
	.uleb128 0x23
	.4byte	.LVL1592
	.4byte	0x1e70
	.uleb128 0x23
	.4byte	.LVL1593
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1594
	.4byte	0x2463
	.uleb128 0x23
	.4byte	.LVL1595
	.4byte	0x29db
	.byte	0
	.uleb128 0x23
	.4byte	.LVL1570
	.4byte	0x43ad
	.uleb128 0x23
	.4byte	.LVL1571
	.4byte	0x3449
	.uleb128 0x23
	.4byte	.LVL1572
	.4byte	0x333c
	.uleb128 0x23
	.4byte	.LVL1573
	.4byte	0x319a
	.uleb128 0x23
	.4byte	.LVL1574
	.4byte	0x31fc
	.uleb128 0x23
	.4byte	.LVL1575
	.4byte	0x2b0c
	.uleb128 0x23
	.4byte	.LVL1576
	.4byte	0x343c
	.uleb128 0x23
	.4byte	.LVL1577
	.4byte	0x4316
	.uleb128 0x23
	.4byte	.LVL1578
	.4byte	0x2b8f
	.uleb128 0x23
	.4byte	.LVL1579
	.4byte	0x2cc3
	.uleb128 0x23
	.4byte	.LVL1580
	.4byte	0x3712
	.byte	0
	.uleb128 0x67
	.4byte	.LASF656
	.4byte	.LASF656
	.byte	0x11
	.byte	0x6f
	.uleb128 0x68
	.4byte	.LASF669
	.4byte	.LASF669
	.uleb128 0x67
	.4byte	.LASF657
	.4byte	.LASF657
	.byte	0x12
	.byte	0x19
	.uleb128 0x67
	.4byte	.LASF658
	.4byte	.LASF658
	.byte	0xb
	.byte	0x8e
	.uleb128 0x67
	.4byte	.LASF659
	.4byte	.LASF659
	.byte	0x12
	.byte	0x1b
	.uleb128 0x67
	.4byte	.LASF660
	.4byte	.LASF660
	.byte	0x13
	.byte	0x27
	.uleb128 0x67
	.4byte	.LASF661
	.4byte	.LASF661
	.byte	0xd
	.byte	0x18
	.uleb128 0x69
	.4byte	.LASF662
	.4byte	.LASF662
	.byte	0xc
	.2byte	0x19b
	.uleb128 0x6a
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
	.uleb128 0x3
	.uleb128 0x26
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
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
	.uleb128 0x8
	.byte	0
	.byte	0
	.uleb128 0x5
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
	.uleb128 0x6
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
	.uleb128 0x13
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3c
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0xe
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
	.uleb128 0xf
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
	.uleb128 0x10
	.uleb128 0x5
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x11
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
	.uleb128 0x12
	.uleb128 0x28
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x1c
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x13
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
	.uleb128 0x14
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
	.uleb128 0x15
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
	.uleb128 0x16
	.uleb128 0x21
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2f
	.uleb128 0xb
	.byte	0
	.byte	0
	.uleb128 0x17
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
	.uleb128 0x18
	.uleb128 0x21
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2f
	.uleb128 0x5
	.byte	0
	.byte	0
	.uleb128 0x19
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
	.uleb128 0x1a
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
	.uleb128 0x1b
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
	.uleb128 0x1c
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
	.uleb128 0x1d
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
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x20
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
	.uleb128 0x21
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
	.uleb128 0x22
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
	.uleb128 0x23
	.uleb128 0x4109
	.byte	0
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x24
	.uleb128 0xf
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
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
	.uleb128 0x32
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.byte	0
	.byte	0
	.uleb128 0x33
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
	.uleb128 0x34
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
	.uleb128 0x35
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
	.uleb128 0x36
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
	.uleb128 0x37
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
	.uleb128 0x38
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x55
	.uleb128 0x17
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x39
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
	.uleb128 0x3a
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
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
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
	.byte	0
	.byte	0
	.uleb128 0x3d
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
	.uleb128 0x3e
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
	.uleb128 0x3f
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
	.uleb128 0x40
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
	.uleb128 0x41
	.uleb128 0xb
	.byte	0x1
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x42
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
	.uleb128 0x43
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
	.uleb128 0x44
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
	.uleb128 0x45
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
	.uleb128 0x46
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
	.uleb128 0x47
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
	.uleb128 0x48
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
	.uleb128 0x49
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
	.uleb128 0x4a
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
	.uleb128 0x4b
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
	.uleb128 0x4c
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
	.uleb128 0x4d
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
	.uleb128 0x4e
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
	.uleb128 0x4f
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
	.uleb128 0x50
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
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x52
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
	.uleb128 0x53
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
	.uleb128 0x54
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
	.uleb128 0x20
	.uleb128 0xb
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x56
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
	.uleb128 0x57
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
	.uleb128 0x58
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
	.uleb128 0x59
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
	.uleb128 0x5a
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
	.uleb128 0x5b
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
	.uleb128 0x5c
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
	.uleb128 0x5d
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
	.uleb128 0x5f
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
	.uleb128 0x60
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x61
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x62
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
	.uleb128 0x63
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x64
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x65
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
.LLST290:
	.4byte	.LVL1635-.Ltext0
	.4byte	.LVL1636-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST291:
	.4byte	.LVL1635-.Ltext0
	.4byte	.LVL1636-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST292:
	.4byte	.LVL1635-.Ltext0
	.4byte	.LVL1636-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST172:
	.4byte	.LVL832-.Ltext0
	.4byte	.LVL833-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST173:
	.4byte	.LVL832-.Ltext0
	.4byte	.LVL833-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST174:
	.4byte	.LVL832-.Ltext0
	.4byte	.LVL833-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST105:
	.4byte	.LVL481-.Ltext0
	.4byte	.LVL483-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL483-1-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST106:
	.4byte	.LVL481-.Ltext0
	.4byte	.LVL482-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL482-.Ltext0
	.4byte	.LFE276-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST107:
	.4byte	.LVL485-.Ltext0
	.4byte	.LVL486-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL488-.Ltext0
	.4byte	.LVL489-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL490-.Ltext0
	.4byte	.LVL491-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST91:
	.4byte	.LVL396-.Ltext0
	.4byte	.LVL400-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL401-.Ltext0
	.4byte	.LVL403-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST92:
	.4byte	.LVL396-.Ltext0
	.4byte	.LVL402-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST93:
	.4byte	.LVL396-.Ltext0
	.4byte	.LVL401-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL401-.Ltext0
	.4byte	.LFE274-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST86:
	.4byte	.LVL371-.Ltext0
	.4byte	.LVL373-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST87:
	.4byte	.LVL371-.Ltext0
	.4byte	.LVL376-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL376-.Ltext0
	.4byte	.LVL386-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL386-.Ltext0
	.4byte	.LVL388-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL393-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST88:
	.4byte	.LVL371-.Ltext0
	.4byte	.LVL374-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL374-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST89:
	.4byte	.LVL371-.Ltext0
	.4byte	.LVL372-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL372-.Ltext0
	.4byte	.LVL385-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL394-.Ltext0
	.4byte	.LVL395-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST90:
	.4byte	.LVL374-.Ltext0
	.4byte	.LVL377-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL378-.Ltext0
	.4byte	.LVL379-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL382-.Ltext0
	.4byte	.LVL383-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL385-.Ltext0
	.4byte	.LVL387-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL393-.Ltext0
	.4byte	.LFE273-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST78:
	.4byte	.LVL338-.Ltext0
	.4byte	.LVL342-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL343-.Ltext0
	.4byte	.LVL346-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL347-.Ltext0
	.4byte	.LVL349-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST79:
	.4byte	.LVL338-.Ltext0
	.4byte	.LVL343-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL343-.Ltext0
	.4byte	.LFE272-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST80:
	.4byte	.LVL338-.Ltext0
	.4byte	.LVL345-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL347-.Ltext0
	.4byte	.LVL348-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST81:
	.4byte	.LVL340-.Ltext0
	.4byte	.LVL344-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL352-.Ltext0
	.4byte	.LVL353-.Ltext0
	.2byte	0x1
	.byte	0x50
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
	.4byte	.LFE271-.Ltext0
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
	.4byte	.LFE271-.Ltext0
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
	.4byte	.LFE271-.Ltext0
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
.LLST77:
	.4byte	.LVL331-.Ltext0
	.4byte	.LVL332-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL332-.Ltext0
	.4byte	.LFE265-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST76:
	.4byte	.LVL328-.Ltext0
	.4byte	.LVL329-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL329-.Ltext0
	.4byte	.LFE264-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST244:
	.4byte	.LVL1280-.Ltext0
	.4byte	.LVL1281-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST163:
	.4byte	.LVL798-.Ltext0
	.4byte	.LVL801-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL801-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST164:
	.4byte	.LVL798-.Ltext0
	.4byte	.LVL801-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL801-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST165:
	.4byte	.LVL803-.Ltext0
	.4byte	.LVL804-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL810-.Ltext0
	.4byte	.LVL811-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL814-.Ltext0
	.4byte	.LVL815-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST166:
	.4byte	.LVL802-.Ltext0
	.4byte	.LVL812-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL813-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST167:
	.4byte	.LVL808-.Ltext0
	.4byte	.LVL809-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL821-.Ltext0
	.4byte	.LVL823-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST168:
	.4byte	.LVL799-.Ltext0
	.4byte	.LVL806-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL807-.Ltext0
	.4byte	.LVL827-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL830-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST169:
	.4byte	.LVL816-.Ltext0
	.4byte	.LVL817-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL818-.Ltext0
	.4byte	.LVL822-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL825-.Ltext0
	.4byte	.LVL826-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST170:
	.4byte	.LVL802-.Ltext0
	.4byte	.LVL831-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST171:
	.4byte	.LVL819-.Ltext0
	.4byte	.LVL820-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL825-.Ltext0
	.4byte	.LVL826-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST240:
	.4byte	.LVL1233-.Ltext0
	.4byte	.LVL1236-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1237-.Ltext0
	.4byte	.LVL1238-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1239-.Ltext0
	.4byte	.LVL1240-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1248-.Ltext0
	.4byte	.LVL1249-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST241:
	.4byte	.LVL1235-.Ltext0
	.4byte	.LVL1237-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1242-.Ltext0
	.4byte	.LVL1246-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1247-.Ltext0
	.4byte	.LFE258-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	0
	.4byte	0
.LLST242:
	.4byte	.LVL1234-.Ltext0
	.4byte	.LVL1241-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1246-.Ltext0
	.4byte	.LVL1247-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1248-.Ltext0
	.4byte	.LVL1249-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST243:
	.4byte	.LVL1251-.Ltext0
	.4byte	.LVL1253-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1257-.Ltext0
	.4byte	.LVL1263-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST71:
	.4byte	.LVL319-.Ltext0
	.4byte	.LVL320-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL320-.Ltext0
	.4byte	.LFE257-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST72:
	.4byte	.LVL319-.Ltext0
	.4byte	.LVL321-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL321-1-.Ltext0
	.4byte	.LVL327-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST73:
	.4byte	.LVL319-.Ltext0
	.4byte	.LVL321-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL321-1-.Ltext0
	.4byte	.LVL327-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST74:
	.4byte	.LVL322-.Ltext0
	.4byte	.LVL326-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST75:
	.4byte	.LVL323-.Ltext0
	.4byte	.LVL324-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL325-.Ltext0
	.4byte	.LFE257-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST70:
	.4byte	.LVL315-.Ltext0
	.4byte	.LVL316-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL317-.Ltext0
	.4byte	.LVL318-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST187:
	.4byte	.LVL905-.Ltext0
	.4byte	.LVL906-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL906-.Ltext0
	.4byte	.LFE252-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST68:
	.4byte	.LVL305-.Ltext0
	.4byte	.LVL306-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL306-.Ltext0
	.4byte	.LFE250-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST69:
	.4byte	.LVL309-.Ltext0
	.4byte	.LVL310-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL312-.Ltext0
	.4byte	.LFE250-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST226:
	.4byte	.LVL1174-.Ltext0
	.4byte	.LVL1176-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1176-.Ltext0
	.4byte	.LFE249-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST179:
	.4byte	.LVL851-.Ltext0
	.4byte	.LVL854-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL854-.Ltext0
	.4byte	.LVL884-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST180:
	.4byte	.LVL852-.Ltext0
	.4byte	.LVL884-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST181:
	.4byte	.LVL852-.Ltext0
	.4byte	.LVL853-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL866-.Ltext0
	.4byte	.LVL876-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL877-.Ltext0
	.4byte	.LVL884-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST182:
	.4byte	.LVL872-.Ltext0
	.4byte	.LVL873-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL874-.Ltext0
	.4byte	.LVL875-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL879-.Ltext0
	.4byte	.LVL880-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL881-.Ltext0
	.4byte	.LVL882-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST183:
	.4byte	.LVL855-.Ltext0
	.4byte	.LVL856-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL857-.Ltext0
	.4byte	.LVL858-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL859-.Ltext0
	.4byte	.LVL860-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST62:
	.4byte	.LVL280-.Ltext0
	.4byte	.LVL282-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL286-.Ltext0
	.4byte	.LVL294-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL297-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST63:
	.4byte	.LVL280-.Ltext0
	.4byte	.LVL282-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL285-.Ltext0
	.4byte	.LVL288-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL297-.Ltext0
	.4byte	.LVL300-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL301-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST64:
	.4byte	.LVL280-.Ltext0
	.4byte	.LVL282-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL284-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST65:
	.4byte	.LVL280-.Ltext0
	.4byte	.LVL282-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL289-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL293-.Ltext0
	.4byte	.LVL297-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST66:
	.4byte	.LVL280-.Ltext0
	.4byte	.LVL281-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL287-.Ltext0
	.4byte	.LVL290-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL291-.Ltext0
	.4byte	.LVL295-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL297-.Ltext0
	.4byte	.LVL298-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL299-.Ltext0
	.4byte	.LFE246-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST57:
	.4byte	.LVL194-.Ltext0
	.4byte	.LVL195-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL195-.Ltext0
	.4byte	.LFE245-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST56:
	.4byte	.LVL189-.Ltext0
	.4byte	.LVL190-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL190-.Ltext0
	.4byte	.LFE243-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST144:
	.4byte	.LVL667-.Ltext0
	.4byte	.LVL670-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL675-.Ltext0
	.4byte	.LVL678-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST145:
	.4byte	.LVL671-.Ltext0
	.4byte	.LVL675-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL678-.Ltext0
	.4byte	.LFE240-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST146:
	.4byte	.LVL672-.Ltext0
	.4byte	.LVL675-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST224:
	.4byte	.LVL1164-.Ltext0
	.4byte	.LVL1166-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1169-.Ltext0
	.4byte	.LFE239-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST225:
	.4byte	.LVL1161-.Ltext0
	.4byte	.LVL1165-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1166-.Ltext0
	.4byte	.LVL1169-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST206:
	.4byte	.LVL1045-.Ltext0
	.4byte	.LVL1046-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1046-.Ltext0
	.4byte	.LFE238-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST160:
	.4byte	.LVL782-.Ltext0
	.4byte	.LVL794-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST161:
	.4byte	.LVL780-.Ltext0
	.4byte	.LVL789-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL790-.Ltext0
	.4byte	.LVL792-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST162:
	.4byte	.LVL779-.Ltext0
	.4byte	.LVL786-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL787-.Ltext0
	.4byte	.LVL794-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST55:
	.4byte	.LVL185-.Ltext0
	.4byte	.LVL186-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL186-.Ltext0
	.4byte	.LFE234-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST191:
	.4byte	.LVL923-.Ltext0
	.4byte	.LVL928-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL928-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST192:
	.4byte	.LVL924-.Ltext0
	.4byte	.LVL925-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL926-.Ltext0
	.4byte	.LVL931-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL932-.Ltext0
	.4byte	.LVL933-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL944-.Ltext0
	.4byte	.LVL945-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL950-.Ltext0
	.4byte	.LVL951-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL954-.Ltext0
	.4byte	.LVL955-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL955-.Ltext0
	.4byte	.LVL956-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL983-.Ltext0
	.4byte	.LVL985-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL992-.Ltext0
	.4byte	.LVL993-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST193:
	.4byte	.LVL925-.Ltext0
	.4byte	.LVL927-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL943-.Ltext0
	.4byte	.LVL952-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL956-.Ltext0
	.4byte	.LVL957-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL957-.Ltext0
	.4byte	.LVL958-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL970-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL974-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL976-.Ltext0
	.4byte	.LVL977-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL979-.Ltext0
	.4byte	.LVL980-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL984-.Ltext0
	.4byte	.LVL987-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL992-.Ltext0
	.4byte	.LVL994-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1042-.Ltext0
	.4byte	.LVL1043-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST194:
	.4byte	.LVL944-.Ltext0
	.4byte	.LVL946-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL987-.Ltext0
	.4byte	.LVL988-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1039-.Ltext0
	.4byte	.LVL1041-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST195:
	.4byte	.LVL940-.Ltext0
	.4byte	.LVL941-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL942-.Ltext0
	.4byte	.LVL944-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL944-.Ltext0
	.4byte	.LVL947-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL963-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL963-.Ltext0
	.4byte	.LVL964-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL986-.Ltext0
	.4byte	.LVL989-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1039-.Ltext0
	.4byte	.LVL1040-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1042-.Ltext0
	.4byte	.LVL1044-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST196:
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL934-.Ltext0
	.4byte	.LVL935-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL935-.Ltext0
	.4byte	.LVL948-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL953-.Ltext0
	.4byte	.LVL965-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL968-.Ltext0
	.4byte	.LVL969-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1042-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST197:
	.4byte	.LVL939-.Ltext0
	.4byte	.LVL941-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL959-.Ltext0
	.4byte	.LVL960-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL963-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST198:
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL973-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL976-.Ltext0
	.4byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL979-.Ltext0
	.4byte	.LVL981-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL990-.Ltext0
	.4byte	.LVL994-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1001-.Ltext0
	.4byte	.LVL1009-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1011-.Ltext0
	.4byte	.LVL1032-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST199:
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	.LVL972-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	0
	.4byte	0
.LLST200:
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL972-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	0
	.4byte	0
.LLST201:
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL1012-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1012-.Ltext0
	.4byte	.LVL1017-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1017-.Ltext0
	.4byte	.LVL1020-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1020-.Ltext0
	.4byte	.LVL1026-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1026-.Ltext0
	.4byte	.LFE231-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST202:
	.4byte	.LVL929-.Ltext0
	.4byte	.LVL930-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL973-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL976-.Ltext0
	.4byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL979-.Ltext0
	.4byte	.LVL981-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL991-.Ltext0
	.4byte	.LVL994-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1009-.Ltext0
	.4byte	.LVL1011-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1032-.Ltext0
	.4byte	.LVL1034-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST203:
	.4byte	.LVL937-.Ltext0
	.4byte	.LVL938-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL949-.Ltext0
	.4byte	.LVL950-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL974-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL976-.Ltext0
	.4byte	.LVL977-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL979-.Ltext0
	.4byte	.LVL980-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL992-.Ltext0
	.4byte	.LVL994-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL995-.Ltext0
	.4byte	.LVL997-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL998-.Ltext0
	.4byte	.LVL999-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1036-.Ltext0
	.4byte	.LVL1037-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1037-.Ltext0
	.4byte	.LVL1038-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1038-.Ltext0
	.4byte	.LVL1039-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST204:
	.4byte	.LVL1005-.Ltext0
	.4byte	.LVL1007-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1014-.Ltext0
	.4byte	.LVL1016-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST205:
	.4byte	.LVL1005-.Ltext0
	.4byte	.LVL1006-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1018-.Ltext0
	.4byte	.LVL1019-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1022-.Ltext0
	.4byte	.LVL1025-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST138:
	.4byte	.LVL635-.Ltext0
	.4byte	.LVL636-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL636-.Ltext0
	.4byte	.LFE230-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST139:
	.4byte	.LVL635-.Ltext0
	.4byte	.LVL637-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL637-1-.Ltext0
	.4byte	.LFE230-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -40
	.4byte	0
	.4byte	0
.LLST140:
	.4byte	.LVL638-.Ltext0
	.4byte	.LVL642-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL646-.Ltext0
	.4byte	.LVL651-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST141:
	.4byte	.LVL640-.Ltext0
	.4byte	.LVL648-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL649-.Ltext0
	.4byte	.LFE230-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST142:
	.4byte	.LVL639-.Ltext0
	.4byte	.LVL641-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL647-.Ltext0
	.4byte	.LVL650-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST53:
	.4byte	.LVL176-.Ltext0
	.4byte	.LVL177-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL177-.Ltext0
	.4byte	.LFE229-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST54:
	.4byte	.LVL176-.Ltext0
	.4byte	.LVL178-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL179-.Ltext0
	.4byte	.LVL180-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL181-.Ltext0
	.4byte	.LVL182-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST152:
	.4byte	.LVL721-.Ltext0
	.4byte	.LVL727-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL727-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST153:
	.4byte	.LVL732-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL737-.Ltext0
	.4byte	.LVL738-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL740-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL751-.Ltext0
	.4byte	.LVL752-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST154:
	.4byte	.LVL736-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL745-.Ltext0
	.4byte	.LVL746-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST155:
	.4byte	.LVL722-.Ltext0
	.4byte	.LVL724-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL730-.Ltext0
	.4byte	.LVL731-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL739-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL745-.Ltext0
	.4byte	.LVL747-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL749-.Ltext0
	.4byte	.LVL751-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL755-.Ltext0
	.4byte	.LVL756-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL759-.Ltext0
	.4byte	.LVL761-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL762-.Ltext0
	.4byte	.LVL765-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL766-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST156:
	.4byte	.LVL723-.Ltext0
	.4byte	.LVL733-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL740-.Ltext0
	.4byte	.LVL745-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL751-.Ltext0
	.4byte	.LFE225-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST157:
	.4byte	.LVL724-.Ltext0
	.4byte	.LVL725-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL735-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL745-.Ltext0
	.4byte	.LVL747-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST158:
	.4byte	.LVL725-.Ltext0
	.4byte	.LVL726-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL728-.Ltext0
	.4byte	.LVL729-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL730-.Ltext0
	.4byte	.LVL731-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL739-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL745-.Ltext0
	.4byte	.LVL747-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL748-.Ltext0
	.4byte	.LVL749-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL750-.Ltext0
	.4byte	.LVL751-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL755-.Ltext0
	.4byte	.LVL756-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL758-.Ltext0
	.4byte	.LVL759-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL760-.Ltext0
	.4byte	.LVL761-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL763-.Ltext0
	.4byte	.LVL764-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL765-.Ltext0
	.4byte	.LVL766-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST159:
	.4byte	.LVL726-.Ltext0
	.4byte	.LVL728-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL731-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL740-.Ltext0
	.4byte	.LVL741-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL751-.Ltext0
	.4byte	.LVL753-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST286:
	.4byte	.LVL1553-.Ltext0
	.4byte	.LVL1556-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1566-.Ltext0
	.4byte	.LVL1568-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1569-.Ltext0
	.4byte	.LFE224-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST52:
	.4byte	.LVL174-.Ltext0
	.4byte	.LVL175-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST150:
	.4byte	.LVL694-.Ltext0
	.4byte	.LVL696-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL697-.Ltext0
	.4byte	.LVL699-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL703-.Ltext0
	.4byte	.LVL704-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL705-.Ltext0
	.4byte	.LVL706-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL714-.Ltext0
	.4byte	.LVL715-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST151:
	.4byte	.LVL695-.Ltext0
	.4byte	.LVL698-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL702-.Ltext0
	.4byte	.LVL706-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL708-.Ltext0
	.4byte	.LVL718-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL719-.Ltext0
	.4byte	.LVL720-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST223:
	.4byte	.LVL1152-.Ltext0
	.4byte	.LVL1157-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1158-.Ltext0
	.4byte	.LFE219-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST147:
	.4byte	.LVL680-.Ltext0
	.4byte	.LVL682-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL682-.Ltext0
	.4byte	.LFE217-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST148:
	.4byte	.LVL683-.Ltext0
	.4byte	.LVL684-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL687-.Ltext0
	.4byte	.LVL688-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST149:
	.4byte	.LVL681-.Ltext0
	.4byte	.LVL683-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST215:
	.4byte	.LVL1115-.Ltext0
	.4byte	.LVL1119-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1121-.Ltext0
	.4byte	.LVL1123-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST216:
	.4byte	.LVL1116-.Ltext0
	.4byte	.LVL1117-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1118-.Ltext0
	.4byte	.LVL1120-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1121-.Ltext0
	.4byte	.LVL1122-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST217:
	.4byte	.LVL1126-.Ltext0
	.4byte	.LVL1131-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1131-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST218:
	.4byte	.LVL1126-.Ltext0
	.4byte	.LVL1127-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1127-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST219:
	.4byte	.LVL1126-.Ltext0
	.4byte	.LVL1128-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1128-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST220:
	.4byte	.LVL1130-.Ltext0
	.4byte	.LVL1132-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1136-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST221:
	.4byte	.LVL1133-.Ltext0
	.4byte	.LVL1134-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1135-.Ltext0
	.4byte	.LVL1137-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1139-.Ltext0
	.4byte	.LVL1140-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST222:
	.4byte	.LVL1129-.Ltext0
	.4byte	.LVL1130-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST211:
	.4byte	.LVL1099-.Ltext0
	.4byte	.LVL1103-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1103-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST212:
	.4byte	.LVL1099-.Ltext0
	.4byte	.LVL1103-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1103-.Ltext0
	.4byte	.LFE213-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST213:
	.4byte	.LVL1099-.Ltext0
	.4byte	.LVL1100-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1102-.Ltext0
	.4byte	.LVL1104-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1111-.Ltext0
	.4byte	.LVL1112-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST214:
	.4byte	.LVL1105-.Ltext0
	.4byte	.LVL1106-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1107-.Ltext0
	.4byte	.LVL1108-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1112-.Ltext0
	.4byte	.LVL1113-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST134:
	.4byte	.LVL623-.Ltext0
	.4byte	.LVL626-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL627-.Ltext0
	.4byte	.LVL628-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST135:
	.4byte	.LVL623-.Ltext0
	.4byte	.LVL627-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL627-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST136:
	.4byte	.LVL623-.Ltext0
	.4byte	.LVL627-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL627-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST137:
	.4byte	.LVL629-.Ltext0
	.4byte	.LVL633-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL634-.Ltext0
	.4byte	.LFE212-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST133:
	.4byte	.LVL620-.Ltext0
	.4byte	.LVL621-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST126:
	.4byte	.LVL591-.Ltext0
	.4byte	.LVL593-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL593-.Ltext0
	.4byte	.LFE208-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST127:
	.4byte	.LVL591-.Ltext0
	.4byte	.LVL593-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL593-.Ltext0
	.4byte	.LFE208-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST128:
	.4byte	.LVL597-.Ltext0
	.4byte	.LVL598-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST129:
	.4byte	.LVL592-.Ltext0
	.4byte	.LVL600-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL601-.Ltext0
	.4byte	.LFE208-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST122:
	.4byte	.LVL574-.Ltext0
	.4byte	.LVL576-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL576-.Ltext0
	.4byte	.LVL579-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL579-.Ltext0
	.4byte	.LVL582-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL582-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST123:
	.4byte	.LVL584-.Ltext0
	.4byte	.LVL585-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL588-.Ltext0
	.4byte	.LVL589-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL590-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST124:
	.4byte	.LVL575-.Ltext0
	.4byte	.LVL581-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST125:
	.4byte	.LVL578-.Ltext0
	.4byte	.LVL579-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL580-.Ltext0
	.4byte	.LFE207-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST130:
	.4byte	.LVL604-.Ltext0
	.4byte	.LVL607-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL607-1-.Ltext0
	.4byte	.LFE206-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST131:
	.4byte	.LVL610-.Ltext0
	.4byte	.LVL611-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL614-.Ltext0
	.4byte	.LVL615-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL616-.Ltext0
	.4byte	.LVL617-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL617-.Ltext0
	.4byte	.LVL618-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL619-.Ltext0
	.4byte	.LFE206-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST132:
	.4byte	.LVL605-.Ltext0
	.4byte	.LVL610-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL612-.Ltext0
	.4byte	.LVL614-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST117:
	.4byte	.LVL553-.Ltext0
	.4byte	.LVL557-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL557-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST118:
	.4byte	.LVL556-.Ltext0
	.4byte	.LVL567-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST119:
	.4byte	.LVL559-.Ltext0
	.4byte	.LVL562-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL563-.Ltext0
	.4byte	.LVL564-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL565-.Ltext0
	.4byte	.LVL568-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL569-.Ltext0
	.4byte	.LVL570-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL573-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST120:
	.4byte	.LVL560-.Ltext0
	.4byte	.LVL563-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL566-.Ltext0
	.4byte	.LVL571-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL572-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST121:
	.4byte	.LVL561-.Ltext0
	.4byte	.LVL563-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL566-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST114:
	.4byte	.LVL539-.Ltext0
	.4byte	.LVL540-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL540-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST115:
	.4byte	.LVL539-.Ltext0
	.4byte	.LVL541-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL541-.Ltext0
	.4byte	.LVL548-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST116:
	.4byte	.LVL545-.Ltext0
	.4byte	.LVL546-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL548-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST43:
	.4byte	.LVL141-.Ltext0
	.4byte	.LVL142-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL142-.Ltext0
	.4byte	.LVL145-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL147-.Ltext0
	.4byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST44:
	.4byte	.LVL143-.Ltext0
	.4byte	.LVL144-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL146-.Ltext0
	.4byte	.LVL148-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL149-.Ltext0
	.4byte	.LVL150-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST42:
	.4byte	.LVL139-.Ltext0
	.4byte	.LVL140-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST39:
	.4byte	.LVL130-.Ltext0
	.4byte	.LVL134-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL134-.Ltext0
	.4byte	.LVL135-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL136-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST40:
	.4byte	.LVL130-.Ltext0
	.4byte	.LVL133-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL134-.Ltext0
	.4byte	.LVL137-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL138-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST41:
	.4byte	.LVL131-.Ltext0
	.4byte	.LVL132-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL134-.Ltext0
	.4byte	.LFE199-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST37:
	.4byte	.LVL123-.Ltext0
	.4byte	.LVL126-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL126-.Ltext0
	.4byte	.LFE198-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST38:
	.4byte	.LVL123-.Ltext0
	.4byte	.LVL125-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL126-.Ltext0
	.4byte	.LVL127-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL128-.Ltext0
	.4byte	.LVL129-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST35:
	.4byte	.LVL113-.Ltext0
	.4byte	.LVL117-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL118-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST25:
	.4byte	.LVL75-.Ltext0
	.4byte	.LVL77-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL78-.Ltext0
	.4byte	.LVL79-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST24:
	.4byte	.LVL73-.Ltext0
	.4byte	.LVL74-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	0
	.4byte	0
.LLST23:
	.4byte	.LVL69-.Ltext0
	.4byte	.LVL70-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST84:
	.4byte	.LVL363-.Ltext0
	.4byte	.LVL364-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST85:
	.4byte	.LVL363-.Ltext0
	.4byte	.LVL365-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL365-.Ltext0
	.4byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST22:
	.4byte	.LVL65-.Ltext0
	.4byte	.LVL66-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL67-.Ltext0
	.4byte	.LVL68-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST21:
	.4byte	.LVL61-.Ltext0
	.4byte	.LVL62-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST20:
	.4byte	.LVL57-.Ltext0
	.4byte	.LVL58-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL58-.Ltext0
	.4byte	.LFE183-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST18:
	.4byte	.LVL51-.Ltext0
	.4byte	.LVL52-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL52-.Ltext0
	.4byte	.LFE182-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST19:
	.4byte	.LVL51-.Ltext0
	.4byte	.LVL53-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL53-.Ltext0
	.4byte	.LVL55-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST210:
	.4byte	.LVL1077-.Ltext0
	.4byte	.LVL1078-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1080-.Ltext0
	.4byte	.LVL1081-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1087-.Ltext0
	.4byte	.LVL1088-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL1091-.Ltext0
	.4byte	.LVL1092-1-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST82:
	.4byte	.LVL356-.Ltext0
	.4byte	.LVL357-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL358-.Ltext0
	.4byte	.LVL359-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL360-.Ltext0
	.4byte	.LFE180-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST83:
	.4byte	.LVL361-.Ltext0
	.4byte	.LVL362-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST207:
	.4byte	.LVL1060-.Ltext0
	.4byte	.LVL1061-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1068-.Ltext0
	.4byte	.LVL1070-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST208:
	.4byte	.LVL1052-.Ltext0
	.4byte	.LVL1057-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1061-.Ltext0
	.4byte	.LVL1062-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1063-.Ltext0
	.4byte	.LVL1064-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1070-.Ltext0
	.4byte	.LFE179-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST209:
	.4byte	.LVL1057-.Ltext0
	.4byte	.LVL1060-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1064-.Ltext0
	.4byte	.LVL1066-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1067-.Ltext0
	.4byte	.LVL1068-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST175:
	.4byte	.LVL834-.Ltext0
	.4byte	.LVL837-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL848-.Ltext0
	.4byte	.LVL850-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST176:
	.4byte	.LVL838-.Ltext0
	.4byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL850-.Ltext0
	.4byte	.LFE176-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST177:
	.4byte	.LVL836-.Ltext0
	.4byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL850-.Ltext0
	.4byte	.LFE176-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST178:
	.4byte	.LVL837-.Ltext0
	.4byte	.LVL844-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL845-.Ltext0
	.4byte	.LVL848-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL850-.Ltext0
	.4byte	.LFE176-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST112:
	.4byte	.LVL534-.Ltext0
	.4byte	.LVL536-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST113:
	.4byte	.LVL534-.Ltext0
	.4byte	.LVL535-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL535-.Ltext0
	.4byte	.LFE175-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST15:
	.4byte	.LVL41-.Ltext0
	.4byte	.LVL42-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL42-.Ltext0
	.4byte	.LFE173-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST16:
	.4byte	.LVL46-.Ltext0
	.4byte	.LVL48-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST17:
	.4byte	.LVL44-.Ltext0
	.4byte	.LVL47-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST14:
	.4byte	.LVL35-.Ltext0
	.4byte	.LVL36-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL36-.Ltext0
	.4byte	.LFE172-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST283:
	.4byte	.LVL1546-.Ltext0
	.4byte	.LVL1549-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST284:
	.4byte	.LVL1546-.Ltext0
	.4byte	.LVL1548-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST285:
	.4byte	.LVL1546-.Ltext0
	.4byte	.LVL1547-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST269:
	.4byte	.LVL1432-.Ltext0
	.4byte	.LVL1435-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST270:
	.4byte	.LVL1432-.Ltext0
	.4byte	.LVL1434-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST271:
	.4byte	.LVL1432-.Ltext0
	.4byte	.LVL1433-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST234:
	.4byte	.LVL1215-.Ltext0
	.4byte	.LVL1216-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1216-.Ltext0
	.4byte	.LVL1222-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST235:
	.4byte	.LVL1215-.Ltext0
	.4byte	.LVL1219-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1219-1-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST236:
	.4byte	.LVL1217-.Ltext0
	.4byte	.LVL1218-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1223-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST237:
	.4byte	.LVL1217-.Ltext0
	.4byte	.LVL1218-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1221-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST238:
	.4byte	.LVL1217-.Ltext0
	.4byte	.LVL1218-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1224-.Ltext0
	.4byte	.LVL1229-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1229-.Ltext0
	.4byte	.LVL1230-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1230-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST239:
	.4byte	.LVL1217-.Ltext0
	.4byte	.LVL1218-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -40
	.4byte	.LVL1224-.Ltext0
	.4byte	.LFE168-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -40
	.4byte	0
	.4byte	0
.LLST272:
	.4byte	.LVL1437-.Ltext0
	.4byte	.LVL1439-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1441-.Ltext0
	.4byte	.LVL1444-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST273:
	.4byte	.LVL1437-.Ltext0
	.4byte	.LVL1440-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1440-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST274:
	.4byte	.LVL1437-.Ltext0
	.4byte	.LVL1438-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1441-.Ltext0
	.4byte	.LVL1442-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1442-.Ltext0
	.4byte	.LVL1443-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1487-.Ltext0
	.4byte	.LVL1488-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1492-.Ltext0
	.4byte	.LVL1493-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1508-.Ltext0
	.4byte	.LVL1509-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1521-.Ltext0
	.4byte	.LVL1522-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST275:
	.4byte	.LVL1437-.Ltext0
	.4byte	.LVL1440-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1441-.Ltext0
	.4byte	.LVL1442-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1493-.Ltext0
	.4byte	.LVL1494-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1512-.Ltext0
	.4byte	.LVL1513-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1514-.Ltext0
	.4byte	.LVL1516-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1534-.Ltext0
	.4byte	.LVL1535-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1539-.Ltext0
	.4byte	.LVL1540-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST276:
	.4byte	.LVL1456-.Ltext0
	.4byte	.LVL1479-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1496-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST277:
	.4byte	.LVL1447-.Ltext0
	.4byte	.LVL1450-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1451-.Ltext0
	.4byte	.LVL1452-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1476-.Ltext0
	.4byte	.LVL1478-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1484-.Ltext0
	.4byte	.LVL1486-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1494-.Ltext0
	.4byte	.LVL1495-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1503-.Ltext0
	.4byte	.LVL1505-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1510-.Ltext0
	.4byte	.LVL1511-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1520-.Ltext0
	.4byte	.LVL1521-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1523-.Ltext0
	.4byte	.LVL1524-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1528-.Ltext0
	.4byte	.LVL1529-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST278:
	.4byte	.LVL1448-.Ltext0
	.4byte	.LVL1452-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1452-.Ltext0
	.4byte	.LVL1454-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1455-.Ltext0
	.4byte	.LVL1460-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1461-.Ltext0
	.4byte	.LVL1463-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1468-.Ltext0
	.4byte	.LVL1471-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1472-.Ltext0
	.4byte	.LVL1475-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1475-.Ltext0
	.4byte	.LVL1478-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1484-.Ltext0
	.4byte	.LVL1485-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1498-.Ltext0
	.4byte	.LVL1499-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1504-.Ltext0
	.4byte	.LVL1506-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1515-.Ltext0
	.4byte	.LVL1516-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1531-.Ltext0
	.4byte	.LVL1532-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1537-.Ltext0
	.4byte	.LVL1538-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1540-.Ltext0
	.4byte	.LVL1542-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST279:
	.4byte	.LVL1440-.Ltext0
	.4byte	.LVL1441-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.4byte	.LVL1457-.Ltext0
	.4byte	.LVL1484-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.4byte	.LVL1490-.Ltext0
	.4byte	.LVL1491-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.4byte	.LVL1496-.Ltext0
	.4byte	.LVL1518-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.4byte	.LVL1518-.Ltext0
	.4byte	.LVL1519-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1519-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.4byte	0
	.4byte	0
.LLST280:
	.4byte	.LVL1440-.Ltext0
	.4byte	.LVL1441-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1457-.Ltext0
	.4byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1473-.Ltext0
	.4byte	.LVL1484-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1490-.Ltext0
	.4byte	.LVL1491-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1498-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST281:
	.4byte	.LVL1449-.Ltext0
	.4byte	.LVL1450-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1450-.Ltext0
	.4byte	.LVL1451-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1452-.Ltext0
	.4byte	.LVL1458-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1458-.Ltext0
	.4byte	.LVL1459-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1469-.Ltext0
	.4byte	.LVL1470-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1474-.Ltext0
	.4byte	.LVL1475-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1540-.Ltext0
	.4byte	.LVL1541-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1544-.Ltext0
	.4byte	.LVL1545-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1545-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST282:
	.4byte	.LVL1453-.Ltext0
	.4byte	.LVL1466-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1467-.Ltext0
	.4byte	.LVL1477-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1496-.Ltext0
	.4byte	.LFE167-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST227:
	.4byte	.LVL1184-.Ltext0
	.4byte	.LVL1188-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1188-1-.Ltext0
	.4byte	.LVL1189-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1194-.Ltext0
	.4byte	.LVL1195-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1205-.Ltext0
	.4byte	.LVL1208-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST228:
	.4byte	.LVL1184-.Ltext0
	.4byte	.LVL1188-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1188-1-.Ltext0
	.4byte	.LVL1208-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST229:
	.4byte	.LVL1184-.Ltext0
	.4byte	.LVL1186-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST230:
	.4byte	.LVL1184-.Ltext0
	.4byte	.LVL1187-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1187-.Ltext0
	.4byte	.LVL1208-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST231:
	.4byte	.LVL1185-.Ltext0
	.4byte	.LVL1208-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST232:
	.4byte	.LVL1188-.Ltext0
	.4byte	.LVL1189-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	.LVL1189-.Ltext0
	.4byte	.LVL1197-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1197-.Ltext0
	.4byte	.LFE165-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	0
	.4byte	0
.LLST233:
	.4byte	.LVL1188-.Ltext0
	.4byte	.LVL1189-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1194-.Ltext0
	.4byte	.LVL1195-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1199-.Ltext0
	.4byte	.LVL1200-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1201-.Ltext0
	.4byte	.LVL1202-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1204-.Ltext0
	.4byte	.LVL1206-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST254:
	.4byte	.LVL1370-.Ltext0
	.4byte	.LVL1372-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1375-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1376-.Ltext0
	.4byte	.LVL1379-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST255:
	.4byte	.LVL1370-.Ltext0
	.4byte	.LVL1374-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1374-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST256:
	.4byte	.LVL1370-.Ltext0
	.4byte	.LVL1371-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1376-.Ltext0
	.4byte	.LVL1377-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1377-.Ltext0
	.4byte	.LVL1378-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1402-.Ltext0
	.4byte	.LVL1404-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST257:
	.4byte	.LVL1370-.Ltext0
	.4byte	.LVL1373-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1373-1-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST258:
	.4byte	.LVL1384-.Ltext0
	.4byte	.LVL1386-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1390-.Ltext0
	.4byte	.LVL1391-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1399-.Ltext0
	.4byte	.LVL1401-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1417-.Ltext0
	.4byte	.LVL1418-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1419-.Ltext0
	.4byte	.LVL1420-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1423-.Ltext0
	.4byte	.LVL1424-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1425-.Ltext0
	.4byte	.LVL1427-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1430-.Ltext0
	.4byte	.LVL1431-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST259:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1375-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1387-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1395-.Ltext0
	.4byte	.LVL1396-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1405-.Ltext0
	.4byte	.LVL1406-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1411-.Ltext0
	.4byte	.LVL1412-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1417-.Ltext0
	.4byte	.LVL1418-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1421-.Ltext0
	.4byte	.LVL1422-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1423-.Ltext0
	.4byte	.LVL1424-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1425-.Ltext0
	.4byte	.LVL1426-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1427-.Ltext0
	.4byte	.LVL1428-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1429-.Ltext0
	.4byte	.LVL1430-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1431-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST260:
	.4byte	.LVL1407-.Ltext0
	.4byte	.LVL1408-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1409-.Ltext0
	.4byte	.LVL1410-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST261:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1385-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST262:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1390-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	0
	.4byte	0
.LLST263:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1390-.Ltext0
	.4byte	.LVL1398-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1413-.Ltext0
	.4byte	.LVL1426-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1430-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST264:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1381-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST265:
	.4byte	.LVL1388-.Ltext0
	.4byte	.LVL1389-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1389-.Ltext0
	.4byte	.LVL1390-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1390-.Ltext0
	.4byte	.LVL1392-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1395-.Ltext0
	.4byte	.LVL1397-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1412-.Ltext0
	.4byte	.LVL1414-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST266:
	.4byte	.LVL1402-.Ltext0
	.4byte	.LVL1403-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1415-.Ltext0
	.4byte	.LVL1416-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST267:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1380-.Ltext0
	.4byte	.LFE164-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST268:
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1376-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1390-.Ltext0
	.4byte	.LVL1400-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1406-.Ltext0
	.4byte	.LVL1413-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST288:
	.4byte	.LVL1600-.Ltext0
	.4byte	.LVL1604-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1618-.Ltext0
	.4byte	.LVL1620-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1621-.Ltext0
	.4byte	.LVL1622-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST289:
	.4byte	.LVL1599-.Ltext0
	.4byte	.LVL1601-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1609-.Ltext0
	.4byte	.LVL1611-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1612-.Ltext0
	.4byte	.LVL1616-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST96:
	.4byte	.LVL412-.Ltext0
	.4byte	.LVL413-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL413-.Ltext0
	.4byte	.LVL415-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL417-.Ltext0
	.4byte	.LVL418-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL436-.Ltext0
	.4byte	.LVL439-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL439-.Ltext0
	.4byte	.LVL440-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST97:
	.4byte	.LVL412-.Ltext0
	.4byte	.LVL414-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL414-.Ltext0
	.4byte	.LVL437-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL439-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST98:
	.4byte	.LVL416-.Ltext0
	.4byte	.LVL444-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL445-.Ltext0
	.4byte	.LVL446-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL447-.Ltext0
	.4byte	.LVL456-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL457-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST99:
	.4byte	.LVL420-.Ltext0
	.4byte	.LVL421-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL422-.Ltext0
	.4byte	.LVL423-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL424-.Ltext0
	.4byte	.LVL425-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL427-.Ltext0
	.4byte	.LVL428-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL434-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST100:
	.4byte	.LVL426-.Ltext0
	.4byte	.LVL428-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL428-.Ltext0
	.4byte	.LVL429-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL431-.Ltext0
	.4byte	.LVL433-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL433-.Ltext0
	.4byte	.LVL438-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL439-.Ltext0
	.4byte	.LVL440-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL449-.Ltext0
	.4byte	.LVL450-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL454-.Ltext0
	.4byte	.LVL455-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL459-.Ltext0
	.4byte	.LVL460-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL461-.Ltext0
	.4byte	.LFE159-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST101:
	.4byte	.LVL421-.Ltext0
	.4byte	.LVL422-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL423-.Ltext0
	.4byte	.LVL425-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL432-.Ltext0
	.4byte	.LVL433-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST102:
	.4byte	.LVL425-.Ltext0
	.4byte	.LVL426-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST111:
	.4byte	.LVL529-.Ltext0
	.4byte	.LVL530-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL532-.Ltext0
	.4byte	.LVL533-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST11:
	.4byte	.LVL23-.Ltext0
	.4byte	.LVL24-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL24-.Ltext0
	.4byte	.LVL30-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST12:
	.4byte	.LVL25-.Ltext0
	.4byte	.LVL26-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST10:
	.4byte	.LVL21-.Ltext0
	.4byte	.LVL22-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST9:
	.4byte	.LVL16-.Ltext0
	.4byte	.LVL18-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL19-.Ltext0
	.4byte	.LFE152-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST61:
	.4byte	.LVL275-.Ltext0
	.4byte	.LVL276-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL276-1-.Ltext0
	.4byte	.LFE151-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST58:
	.4byte	.LVL196-.Ltext0
	.4byte	.LVL197-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL197-1-.Ltext0
	.4byte	.LFE150-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST59:
	.4byte	.LVL268-.Ltext0
	.4byte	.LVL269-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL270-.Ltext0
	.4byte	.LVL271-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST60:
	.4byte	.LVL264-.Ltext0
	.4byte	.LVL265-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL272-.Ltext0
	.4byte	.LVL273-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL274-.Ltext0
	.4byte	.LFE150-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST8:
	.4byte	.LVL14-.Ltext0
	.4byte	.LVL15-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST13:
	.4byte	.LVL33-.Ltext0
	.4byte	.LVL34-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL34-.Ltext0
	.4byte	.LFE158-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST26:
	.4byte	.LVL80-.Ltext0
	.4byte	.LVL105-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL106-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST27:
	.4byte	.LVL81-.Ltext0
	.4byte	.LVL108-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST28:
	.4byte	.LVL82-.Ltext0
	.4byte	.LVL103-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL104-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST29:
	.4byte	.LVL85-.Ltext0
	.4byte	.LVL87-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST30:
	.4byte	.LVL92-.Ltext0
	.4byte	.LVL93-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL93-.Ltext0
	.4byte	.LVL95-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST31:
	.4byte	.LVL86-.Ltext0
	.4byte	.LVL88-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL95-.Ltext0
	.4byte	.LVL96-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL98-.Ltext0
	.4byte	.LVL99-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL100-.Ltext0
	.4byte	.LVL102-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST32:
	.4byte	.LVL83-.Ltext0
	.4byte	.LVL84-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL94-.Ltext0
	.4byte	.LVL97-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL98-.Ltext0
	.4byte	.LVL101-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL104-.Ltext0
	.4byte	.LVL106-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL107-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST33:
	.4byte	.LVL83-.Ltext0
	.4byte	.LVL84-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	.LVL89-.Ltext0
	.4byte	.LVL90-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	.LVL91-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	0
	.4byte	0
.LLST34:
	.4byte	.LVL109-.Ltext0
	.4byte	.LVL110-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST36:
	.4byte	.LVL119-.Ltext0
	.4byte	.LVL120-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST45:
	.4byte	.LVL151-.Ltext0
	.4byte	.LVL152-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL152-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST46:
	.4byte	.LVL154-.Ltext0
	.4byte	.LVL157-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST47:
	.4byte	.LVL159-.Ltext0
	.4byte	.LVL161-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL164-.Ltext0
	.4byte	.LVL165-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST48:
	.4byte	.LVL160-.Ltext0
	.4byte	.LVL162-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL164-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST49:
	.4byte	.LVL167-.Ltext0
	.4byte	.LVL168-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST50:
	.4byte	.LVL169-.Ltext0
	.4byte	.LVL170-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL171-.Ltext0
	.4byte	.LVL173-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST51:
	.4byte	.LVL169-.Ltext0
	.4byte	.LVL170-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL171-.Ltext0
	.4byte	.LVL172-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST67:
	.4byte	.LVL302-.Ltext0
	.4byte	.LVL303-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL303-.Ltext0
	.4byte	.LVL304-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST94:
	.4byte	.LVL407-.Ltext0
	.4byte	.LVL408-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL408-.Ltext0
	.4byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST95:
	.4byte	.LVL407-.Ltext0
	.4byte	.LVL409-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL411-.Ltext0
	.4byte	.LFE190-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST103:
	.4byte	.LVL466-.Ltext0
	.4byte	.LVL470-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL470-.Ltext0
	.4byte	.LFE205-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST104:
	.4byte	.LVL472-.Ltext0
	.4byte	.LVL473-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL473-.Ltext0
	.4byte	.LVL474-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL475-.Ltext0
	.4byte	.LVL476-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST108:
	.4byte	.LVL492-.Ltext0
	.4byte	.LVL493-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL493-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST109:
	.4byte	.LVL492-.Ltext0
	.4byte	.LVL493-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL493-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST110:
	.4byte	.LVL492-.Ltext0
	.4byte	.LVL493-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL493-.Ltext0
	.4byte	.LFE277-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST143:
	.4byte	.LVL661-.Ltext0
	.4byte	.LVL666-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -112
	.4byte	0
	.4byte	0
.LLST184:
	.4byte	.LVL885-.Ltext0
	.4byte	.LVL886-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL886-.Ltext0
	.4byte	.LVL887-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST185:
	.4byte	.LVL888-.Ltext0
	.4byte	.LVL895-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL896-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST186:
	.4byte	.LVL897-.Ltext0
	.4byte	.LVL898-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL898-.Ltext0
	.4byte	.LFE251-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST188:
	.4byte	.LVL910-.Ltext0
	.4byte	.LVL920-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST189:
	.4byte	.LVL911-.Ltext0
	.4byte	.LVL920-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -68
	.4byte	0
	.4byte	0
.LLST190:
	.4byte	.LVL913-.Ltext0
	.4byte	.LVL914-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL916-.Ltext0
	.4byte	.LVL920-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST245:
	.4byte	.LVL1289-.Ltext0
	.4byte	.LVL1290-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1290-.Ltext0
	.4byte	.LVL1298-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1306-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST246:
	.4byte	.LVL1289-.Ltext0
	.4byte	.LVL1291-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST247:
	.4byte	.LVL1299-.Ltext0
	.4byte	.LVL1300-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1301-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1306-.Ltext0
	.4byte	.LVL1307-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1308-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1314-.Ltext0
	.4byte	.LVL1315-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1319-.Ltext0
	.4byte	.LVL1331-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1332-.Ltext0
	.4byte	.LVL1355-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1365-.Ltext0
	.4byte	.LVL1368-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST248:
	.4byte	.LVL1345-.Ltext0
	.4byte	.LVL1346-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1347-.Ltext0
	.4byte	.LVL1348-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST249:
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1311-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1319-.Ltext0
	.4byte	.LVL1350-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1350-.Ltext0
	.4byte	.LVL1351-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1351-.Ltext0
	.4byte	.LFE269-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	0
	.4byte	0
.LLST250:
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1312-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1332-.Ltext0
	.4byte	.LVL1333-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1335-.Ltext0
	.4byte	.LVL1339-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1340-.Ltext0
	.4byte	.LVL1354-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST251:
	.4byte	.LVL1337-.Ltext0
	.4byte	.LVL1338-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST252:
	.4byte	.LVL1328-.Ltext0
	.4byte	.LVL1329-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1330-.Ltext0
	.4byte	.LVL1336-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1341-.Ltext0
	.4byte	.LVL1342-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1354-.Ltext0
	.4byte	.LVL1356-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1356-.Ltext0
	.4byte	.LVL1357-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1358-.Ltext0
	.4byte	.LVL1359-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST253:
	.4byte	.LVL1304-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1312-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1326-.Ltext0
	.4byte	.LVL1334-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1344-.Ltext0
	.4byte	.LVL1366-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1368-.Ltext0
	.4byte	.LFE269-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST287:
	.4byte	.LVL1581-.Ltext0
	.4byte	.LVL1582-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1589-.Ltext0
	.4byte	.LVL1590-.Ltext0
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
	.4byte	.LBB226-.Ltext0
	.4byte	.LBE226-.Ltext0
	.4byte	.LBB227-.Ltext0
	.4byte	.LBE227-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB233-.Ltext0
	.4byte	.LBE233-.Ltext0
	.4byte	.LBB238-.Ltext0
	.4byte	.LBE238-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB244-.Ltext0
	.4byte	.LBE244-.Ltext0
	.4byte	.LBB251-.Ltext0
	.4byte	.LBE251-.Ltext0
	.4byte	.LBB252-.Ltext0
	.4byte	.LBE252-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB253-.Ltext0
	.4byte	.LBE253-.Ltext0
	.4byte	.LBB256-.Ltext0
	.4byte	.LBE256-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB259-.Ltext0
	.4byte	.LBE259-.Ltext0
	.4byte	.LBB262-.Ltext0
	.4byte	.LBE262-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB277-.Ltext0
	.4byte	.LBE277-.Ltext0
	.4byte	.LBB280-.Ltext0
	.4byte	.LBE280-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB297-.Ltext0
	.4byte	.LBE297-.Ltext0
	.4byte	.LBB298-.Ltext0
	.4byte	.LBE298-.Ltext0
	.4byte	.LBB299-.Ltext0
	.4byte	.LBE299-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB313-.Ltext0
	.4byte	.LBE313-.Ltext0
	.4byte	.LBB316-.Ltext0
	.4byte	.LBE316-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB317-.Ltext0
	.4byte	.LBE317-.Ltext0
	.4byte	.LBB318-.Ltext0
	.4byte	.LBE318-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB337-.Ltext0
	.4byte	.LBE337-.Ltext0
	.4byte	.LBB342-.Ltext0
	.4byte	.LBE342-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB343-.Ltext0
	.4byte	.LBE343-.Ltext0
	.4byte	.LBB348-.Ltext0
	.4byte	.LBE348-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB359-.Ltext0
	.4byte	.LBE359-.Ltext0
	.4byte	.LBB366-.Ltext0
	.4byte	.LBE366-.Ltext0
	.4byte	.LBB367-.Ltext0
	.4byte	.LBE367-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB375-.Ltext0
	.4byte	.LBE375-.Ltext0
	.4byte	.LBB376-.Ltext0
	.4byte	.LBE376-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB384-.Ltext0
	.4byte	.LBE384-.Ltext0
	.4byte	.LBB389-.Ltext0
	.4byte	.LBE389-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB390-.Ltext0
	.4byte	.LBE390-.Ltext0
	.4byte	.LBB391-.Ltext0
	.4byte	.LBE391-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB398-.Ltext0
	.4byte	.LBE398-.Ltext0
	.4byte	.LBB400-.Ltext0
	.4byte	.LBE400-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB442-.Ltext0
	.4byte	.LBE442-.Ltext0
	.4byte	.LBB449-.Ltext0
	.4byte	.LBE449-.Ltext0
	.4byte	.LBB450-.Ltext0
	.4byte	.LBE450-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB451-.Ltext0
	.4byte	.LBE451-.Ltext0
	.4byte	.LBB454-.Ltext0
	.4byte	.LBE454-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB457-.Ltext0
	.4byte	.LBE457-.Ltext0
	.4byte	.LBB460-.Ltext0
	.4byte	.LBE460-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB469-.Ltext0
	.4byte	.LBE469-.Ltext0
	.4byte	.LBB470-.Ltext0
	.4byte	.LBE470-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB472-.Ltext0
	.4byte	.LBE472-.Ltext0
	.4byte	.LBB473-.Ltext0
	.4byte	.LBE473-.Ltext0
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
	.4byte	.LBB527-.Ltext0
	.4byte	.LBE527-.Ltext0
	.4byte	.LBB528-.Ltext0
	.4byte	.LBE528-.Ltext0
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
	.4byte	.LBB545-.Ltext0
	.4byte	.LBE545-.Ltext0
	.4byte	.LBB546-.Ltext0
	.4byte	.LBE546-.Ltext0
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
.LASF616:
	.ascii	"FtlCacheWriteBack\000"
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
.LASF663:
	.ascii	"GNU C89 6.3.1 20170404 -mlittle-endian -mapcs -mno-"
	.ascii	"sched-prolog -mabi=apcs-gnu -mno-thumb-interwork -m"
	.ascii	"arm -march=armv7-a -mfloat-abi=soft -mtune=cortex-a"
	.ascii	"9 -mfpu=vfpv3-d16 -mtls-dialect=gnu -g -Os -std=gnu"
	.ascii	"90 -fno-strict-aliasing -fno-common -fno-PIE -fno-d"
	.ascii	"warf2-cfi-asm -fno-ipa-sra -fno-delete-null-pointer"
	.ascii	"-checks -fno-stack-protector -fno-omit-frame-pointe"
	.ascii	"r -fno-optimize-sibling-calls -fno-var-tracking-ass"
	.ascii	"ignments -fno-strict-overflow -fno-merge-all-consta"
	.ascii	"nts -fmerge-constants -fstack-check=no -fconserve-s"
	.ascii	"tack --param allow-store-data-races=0\000"
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
