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
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_addr_tran, %function
l2p_addr_tran:
	.fnstart
.LFB261:
	.file 1 "drivers/rkflash/rksftl/sftl_flash.c"
	.loc 1 18 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL0:
	.movsp ip
	mov	ip, sp
.LCFI0:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI1:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI2:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 18 0
	mov	r8, r1
	mov	r9, r2
	.loc 1 25 0
	ldr	r3, .L3
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
	.loc 1 34 0
	uxth	r0, r7
.LVL5:
	.loc 1 35 0
	uxth	r7, r7
	.loc 1 27 0
	cmp	r3, #4
	.loc 1 28 0
	lsreq	r4, r4, #1
	.loc 1 29 0
	lsleq	r5, r5, #1
.LVL6:
	.loc 1 34 0
	mov	r1, r4
.LVL7:
	.loc 1 29 0
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
	mov	r0, #0
.LVL13:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L4:
	.align	2
.L3:
	.word	g_nand_phy_info
.LFE261:
	.fnend
	.size	l2p_addr_tran, .-l2p_addr_tran
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode.part.6, %function
ftl_set_blk_mode.part.6:
	.fnstart
.LFB276:
	.file 2 "drivers/rkflash/rksftl/sftl_sys.c"
	.loc 2 1346 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL14:
	.movsp ip
	mov	ip, sp
.LCFI3:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI4:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI5:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1346 0
	.loc 2 1349 0
	lsr	r1, r0, #5
	mov	ip, #1
	ldr	r3, .L6
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
.LFE276:
	.fnend
	.size	ftl_set_blk_mode.part.6, .-ftl_set_blk_mode.part.6
	.align	2
	.global	Ftl_log2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
.LFB142:
	.file 3 "drivers/rkflash/rksftl/sftl_plat.c"
	.loc 3 128 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL16:
	.movsp ip
	mov	ip, sp
.LCFI6:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI7:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI8:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 128 0
	.loc 3 132 0
	mov	r1, #0
	.loc 3 130 0
	mov	r2, #1
.LVL17:
.L9:
	.loc 3 132 0 discriminator 1
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L10
	.loc 3 134 0
	sub	r0, r3, #1
.LVL18:
	.loc 3 136 0
	uxth	r0, r0
	ldmfd	sp, {fp, sp, pc}
.LVL19:
.L10:
	.loc 3 133 0 discriminator 3
	lsl	r2, r2, #1
	b	.L9
.LFE142:
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo, %function
FtlPrintInfo:
	.fnstart
.LFB143:
	.loc 3 159 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL20:
	.movsp ip
	mov	ip, sp
.LCFI9:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI10:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI11:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 159 0
	ldmfd	sp, {fp, sp, pc}
.LFE143:
	.fnend
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	2
	.global	FtlSysBlkNumInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
.LFB144:
	.loc 3 165 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL21:
	.movsp ip
	mov	ip, sp
.LCFI12:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI13:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI14:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 165 0
	cmp	r0, #24
	.loc 3 168 0
	ldr	r3, .L13
	movcc	r0, #24
	.loc 3 169 0
	ldrh	r2, [r3, #8]
	.loc 3 170 0
	ldrh	r1, [r3, #18]
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
.LFE144:
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
	.align	2
	.global	FtlConstantsInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
.LFB145:
	.loc 3 183 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL23:
	.movsp ip
	mov	ip, sp
.LCFI15:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI16:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI17:
	push	{lr}
	bl	__gnu_mcount_nc
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
	.loc 3 198 0
	strh	r2, [r4, #68]	@ movhi
	.loc 3 204 0
	uxth	r6, r6
	.loc 3 202 0
	mov	r3, #640	@ movhi
	.loc 3 205 0
	smulbb	r0, r0, r5
	.loc 3 199 0
	mov	r2, #0
	.loc 3 201 0
	strheq	r1, [r4, #68]	@ movhi
	.loc 3 202 0
	strh	r3, [r4, #72]	@ movhi
	.loc 3 205 0
	uxth	r0, r0
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
	mov	r7, #640
.LVL29:
	.loc 3 214 0
	strh	r3, [r4, #90]	@ movhi
	.loc 3 216 0
	mul	r3, r5, r6
	.loc 3 212 0
	strh	r2, [r4, #86]	@ movhi
	.loc 3 213 0
	lsr	r2, r2, #8
	.loc 3 223 0
	lsl	r5, r5, #6
	.loc 3 218 0
	mul	r1, r9, r10
	.loc 3 213 0
	strh	r2, [r4, #88]	@ movhi
	.loc 3 221 0
	asr	r7, r7, r8
	.loc 3 216 0
	str	r3, [r4, #24]
	.loc 3 217 0
	mul	r3, r10, r3
	.loc 3 223 0
	add	r8, r8, #9
	.loc 3 218 0
	mov	r0, #5120
	.loc 3 223 0
	asr	r5, r5, r8
	.loc 3 221 0
	add	r7, r7, #2
	.loc 3 217 0
	mul	r3, r9, r3
	asr	r3, r3, #11
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
	movls	r3, #4
	.loc 3 218 0
	strhhi	r0, [r4, #96]	@ movhi
	.loc 3 220 0
	strhls	r3, [r4, #96]	@ movhi
	.loc 3 224 0
	mul	r3, r6, r5
	.loc 3 226 0
	ldrh	r0, [r4, #96]
	add	r5, r5, #8
	.loc 3 224 0
	str	r3, [r4, #104]
	.loc 3 226 0
	bl	__aeabi_uidiv
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
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.word	.LANCHOR0+36
.LFE145:
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
.LFB148:
	.loc 3 514 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL33:
	.movsp ip
	mov	ip, sp
.LCFI18:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI19:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI20:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 514 0
	.loc 3 517 0
	ldr	r2, .L34
	ldrh	r3, [r2, #120]
	cmp	r3, #0
.LBB218:
.LBB219:
	.loc 3 519 0
	ldrne	r3, [r2, #124]
	ldrhne	r2, [r2, #96]
	addne	r2, r3, r2, lsl #1
.LBE219:
.LBE218:
	.loc 3 517 0
	bne	.L29
.L33:
	.loc 3 524 0
	mov	r0, #0
.LVL34:
	ldmfd	sp, {fp, sp, pc}
.LVL35:
.L30:
.LBB221:
.LBB220:
	.loc 3 519 0
	ldrh	r1, [r3], #2
	cmp	r0, r1
	beq	.L32
.L29:
	.loc 3 518 0
	cmp	r3, r2
	bne	.L30
	b	.L33
.L32:
	.loc 3 520 0
	mov	r0, #1
.LVL36:
.LBE220:
.LBE221:
	.loc 3 525 0
	ldmfd	sp, {fp, sp, pc}
.L35:
	.align	2
.L34:
	.word	.LANCHOR0
.LFE148:
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	sftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
	.fnstart
.LFB161:
	.loc 3 1212 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI21:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI22:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI23:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 1212 0
	.loc 3 1214 0
	ldr	r3, .L37
	ldr	r0, [r3, #116]
	ldmfd	sp, {fp, sp, pc}
.L38:
	.align	2
.L37:
	.word	.LANCHOR0
.LFE161:
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.align	2
	.global	FtlBbmMapBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
.LFB162:
	.file 4 "drivers/rkflash/rksftl/sftl_bbm.c"
	.loc 4 5 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL37:
	.movsp ip
	mov	ip, sp
.LCFI24:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI25:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI26:
	.pad #8
	sub	sp, sp, #8
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 5 0
	.loc 4 10 0
	ldr	r4, .L40
	.loc 4 5 0
	mov	r5, r0
	.loc 4 10 0
	ldrh	r7, [r4, #74]
	mov	r1, r7
	bl	__aeabi_uidiv
.LVL38:
	uxth	r6, r0
.LVL39:
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
.LVL40:
	.loc 4 13 0
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	.loc 4 15 0
	ldr	r2, [r2, #156]
	lsr	r1, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	ldr	r0, [r2, r1, lsl #2]
	orr	r0, r0, lr, lsl ip
	str	r0, [r2, r1, lsl #2]
	.loc 4 16 0
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L40+4
	bl	printk
.LVL41:
	.loc 4 17 0
	ldrh	r3, [r4, #134]
	.loc 4 19 0
	mov	r0, #0
	.loc 4 17 0
	add	r3, r3, #1
	strh	r3, [r4, #134]	@ movhi
	.loc 4 19 0
	sub	sp, fp, #28
.LCFI27:
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL42:
.L41:
	.align	2
.L40:
	.word	.LANCHOR0
	.word	.LC0
.LFE162:
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
.LFB163:
	.loc 4 24 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL43:
	.movsp ip
	mov	ip, sp
.LCFI28:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI29:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI30:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 24 0
	mov	r7, r0
	.loc 4 30 0
	ldr	r5, .L43
	ldrh	r6, [r5, #74]
	.loc 4 31 0
	mov	r1, r6
	bl	__aeabi_uidivmod
.LVL44:
	.loc 4 30 0
	mov	r0, r7
	.loc 4 31 0
	uxth	r4, r1
.LVL45:
	.loc 4 30 0
	mov	r1, r6
	bl	__aeabi_uidiv
.LVL46:
	uxth	r0, r0
.LVL47:
	.loc 4 34 0
	lsr	r2, r4, #5
	.loc 4 33 0
	add	r5, r5, r0, lsl #2
	.loc 4 34 0
	and	r4, r4, #31
.LVL48:
	ldr	r3, [r5, #156]
	ldr	r0, [r3, r2, lsl #2]
.LVL49:
	lsr	r0, r0, r4
	.loc 4 36 0
	and	r0, r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L44:
	.align	2
.L43:
	.word	.LANCHOR0
.LFE163:
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
	.fnstart
.LFB164:
	.loc 4 39 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI31:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI32:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI33:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 39 0
	ldmfd	sp, {fp, sp, pc}
.LFE164:
	.fnend
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	2
	.global	FtlBbtMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
.LFB167:
	.loc 4 149 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI34:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI35:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI36:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 149 0
	.loc 4 150 0
	mvn	r3, #0
.LBB227:
.LBB228:
.LBB229:
	.loc 1 202 0
	mov	r2, #16
.LBE229:
.LBE228:
.LBE227:
	.loc 4 150 0
	ldr	r0, .L47
.LBB234:
.LBB232:
.LBB230:
	.loc 1 202 0
	mov	r1, #255
.LBE230:
.LBE232:
.LBE234:
	.loc 4 150 0
	strh	r3, [r0, #128]	@ movhi
	.loc 4 151 0
	mov	r3, #0
	strh	r3, [r0, #134]	@ movhi
.LBB235:
.LBB233:
.LBB231:
	.loc 1 202 0
	add	r0, r0, #140
	bl	memset
.LVL50:
	ldmfd	sp, {fp, sp, pc}
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
.LBE231:
.LBE233:
.LBE235:
.LFE167:
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
.LFB168:
	.loc 4 157 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI37:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI38:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI39:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 157 0
	.loc 4 160 0
	mov	r5, #0
	ldr	r3, .L56
	.loc 4 159 0
	mov	r4, r5
	.loc 4 160 0
	ldrh	r2, [r3, #74]
	ldrh	r6, [r3, #30]
	mul	r6, r6, r2
.L50:
	uxth	r0, r5
.LVL51:
	.loc 4 160 0 is_stmt 0 discriminator 1
	cmp	r0, r6
.LVL52:
	blt	.L52
	.loc 4 169 0 is_stmt 1
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L52:
	.loc 4 162 0
	bl	FtlBbmIsBadBlock
.LVL53:
	cmp	r0, #0
	add	r5, r5, #1
	.loc 4 164 0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L50
.L57:
	.align	2
.L56:
	.word	.LANCHOR0
.LFE168:
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
.LFB172:
	.loc 2 5 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL54:
	.movsp ip
	mov	ip, sp
.LCFI40:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI41:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI42:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 5 0
	mov	r5, r1
	mov	r7, r0
	.loc 2 8 0
	ldr	r4, .L59
	ldrh	r6, [r4, #32]
	mov	r1, r6
.LVL55:
	bl	__aeabi_uidiv
.LVL56:
	ldrh	r4, [r4, #74]
	smulbb	r5, r6, r5
.LVL57:
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
.LVL58:
	add	r0, r5, r1
	add	r0, r4, r0
	.loc 2 10 0
	uxth	r0, r0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L60:
	.align	2
.L59:
	.word	.LANCHOR0
.LFE172:
	.fnend
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
.LFB173:
	.loc 2 13 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL59:
	.movsp ip
	mov	ip, sp
.LCFI43:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI44:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI45:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 13 0
	mov	r6, r0
	.loc 2 16 0
	ldr	r3, .L62
	ldrh	r5, [r3, #32]
	ldrh	r1, [r3, #74]
	bl	__aeabi_uidiv
.LVL60:
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
.LVL61:
	add	r1, r4, r1
	.loc 2 18 0
	uxth	r0, r1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L63:
	.align	2
.L62:
	.word	.LANCHOR0
.LFE173:
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
.LFB174:
	.loc 2 21 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL62:
	.movsp ip
	mov	ip, sp
.LCFI46:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI47:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI48:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 21 0
	.loc 2 24 0
	ldr	r4, .L65
	ldrh	r1, [r4, #74]
	bl	__aeabi_uidivmod
.LVL63:
	uxth	r0, r1
	.loc 2 25 0
	ldrh	r1, [r4, #32]
	bl	__aeabi_uidiv
.LVL64:
	.loc 2 27 0
	uxth	r0, r0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L66:
	.align	2
.L65:
	.word	.LANCHOR0
.LFE174:
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
.LFB175:
	.loc 2 31 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL65:
	.movsp ip
	mov	ip, sp
.LCFI49:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI50:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI51:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 31 0
	.loc 2 34 0
	cmp	r0, r1
	bls	.L68
	.loc 2 36 0
	sub	r0, r0, r1
.LVL66:
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.LVL67:
.L68:
	.loc 2 40 0
	sub	r0, r1, r0
.LVL68:
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	.loc 2 44 0
	ldmfd	sp, {fp, sp, pc}
.LFE175:
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
.LFB177:
	.loc 2 76 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL69:
	.movsp ip
	mov	ip, sp
.LCFI52:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI53:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI54:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 76 0
	.loc 2 79 0
	mov	r4, #0
.LBB241:
.LBB242:
.LBB243:
	.loc 1 202 0
	mov	r1, #2048
.LBE243:
.LBE242:
.LBE241:
	.loc 2 79 0
	ldr	r3, .L71
	.loc 2 82 0
	strh	r0, [r3, #188]	@ movhi
.LBB248:
.LBB246:
.LBB244:
	.loc 1 202 0
	add	r0, r3, #196
.LVL70:
.LBE244:
.LBE246:
.LBE248:
	.loc 2 79 0
	strh	r4, [r3, #190]	@ movhi
	.loc 2 80 0
	strh	r4, [r3, #192]	@ movhi
	.loc 2 81 0
	strh	r4, [r3, #194]	@ movhi
.LBB249:
.LBB247:
.LBB245:
	.loc 1 202 0
	bl	__memzero
.LVL71:
.LBE245:
.LBE247:
.LBE249:
	.loc 2 85 0
	mov	r0, r4
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L72:
	.align	2
.L71:
	.word	.LANCHOR0
.LFE177:
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
.LFB178:
	.loc 2 88 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI55:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI56:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI57:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 88 0
	.loc 2 89 0
	ldr	r3, .L74
	ldrh	r0, [r3, #194]
	.loc 2 90 0
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L75:
	.align	2
.L74:
	.word	.LANCHOR0
.LFE178:
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
.LFB179:
	.loc 2 93 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI58:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI59:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI60:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 93 0
	.loc 2 94 0
	ldr	r3, .L77
	ldrh	r0, [r3, #194]
	.loc 2 95 0
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L78:
	.align	2
.L77:
	.word	.LANCHOR0
.LFE179:
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
.LFB181:
	.loc 2 116 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI61:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI62:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI63:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 116 0
	.loc 2 123 0
	mov	r3, #0
	ldr	r6, .L88
	mov	r4, r6
.L80:
	.loc 2 123 0 is_stmt 0 discriminator 1
	ldrh	r1, [r6, #194]
	uxth	r2, r3
	add	r7, r3, #1
	cmp	r1, r2
	bhi	.L81
	mov	r1, #0
.LVL72:
.L82:
	.loc 2 128 0 is_stmt 1 discriminator 1
	ldrh	lr, [r4, #194]
.LVL73:
	sub	r3, lr, #1
.LVL74:
	cmp	r1, r3
	blt	.L87
	.loc 2 144 0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL75:
.L81:
	.loc 2 124 0 discriminator 3
	ldrh	r2, [r6, #190]
	uxth	r5, r3
	add	r3, r2, r5
	add	r3, r6, r3, lsl #1
	ldrh	r0, [r3, #196]
	bl	P2V_block_in_plane
.LVL76:
	.loc 2 125 0 discriminator 3
	ldr	r2, [r6, #2248]
	lsl	r0, r0, #1
	ldr	r3, [r6, #2244]
	ldrh	r2, [r2, r0]
	str	r2, [r3, r5, lsl #2]
	mov	r3, r7
	b	.L80
.LVL77:
.L87:
	.loc 2 130 0
	add	r0, r1, #1
	.loc 2 131 0
	ldr	ip, [r4, #2244]
.LVL78:
	.loc 2 130 0
	uxth	r0, r0
	mov	r3, r1
.LVL79:
	mov	r2, r0
.L83:
	.loc 2 130 0 is_stmt 0 discriminator 1
	cmp	r2, lr
	bcc	.L85
	.loc 2 134 0 is_stmt 1
	cmp	r1, r3
	beq	.L86
	.loc 2 136 0
	ldr	r2, [ip, r1, lsl #2]
	.loc 2 135 0
	ldr	lr, [ip, r3, lsl #2]
.LVL80:
	.loc 2 136 0
	str	r2, [ip, r3, lsl #2]
	.loc 2 137 0
	ldr	r2, [r4, #2244]
	str	lr, [r2, r1, lsl #2]
	.loc 2 139 0
	ldrh	r2, [r4, #190]
	add	r3, r3, r2
.LVL81:
	.loc 2 140 0
	add	r2, r1, r2
	.loc 2 139 0
	add	r3, r3, #4
	.loc 2 140 0
	add	r2, r2, #4
	.loc 2 139 0
	add	r3, r4, r3, lsl #1
	.loc 2 140 0
	add	r2, r4, r2, lsl #1
	.loc 2 139 0
	ldrh	ip, [r3, #188]
.LVL82:
	.loc 2 140 0
	ldrh	r1, [r2, #188]
	strh	r1, [r3, #188]	@ movhi
	.loc 2 141 0
	strh	ip, [r2, #188]	@ movhi
.LVL83:
.L86:
	.loc 2 130 0 discriminator 1
	mov	r1, r0
	b	.L82
.LVL84:
.L85:
	.loc 2 131 0
	ldr	r6, [ip, r3, lsl #2]
	ldr	r5, [ip, r2, lsl #2]
	cmp	r6, r5
	movhi	r3, r2
	.loc 2 130 0
	add	r2, r2, #1
	uxth	r2, r2
	b	.L83
.L89:
	.align	2
.L88:
	.word	.LANCHOR0
.LFE181:
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
.LFB182:
	.loc 2 147 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL85:
	.movsp ip
	mov	ip, sp
.LCFI64:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI65:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI66:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 147 0
.LBB250:
.LBB251:
	.loc 2 94 0
	ldr	r3, .L96
	ldrh	ip, [r3, #194]
.LBE251:
.LBE250:
	.loc 2 152 0
	cmp	ip, #1024
	.loc 2 155 0
	ldrhne	lr, [r3, #190]
	movne	r1, #0
.LVL86:
	.loc 2 152 0
	bne	.L92
.L94:
	.loc 2 148 0
	mov	r0, #0
.LVL87:
	ldmfd	sp, {fp, sp, pc}
.LVL88:
.L93:
	.loc 2 155 0
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #196]
	cmp	r2, r0
	beq	.L95
	.loc 2 154 0 discriminator 2
	add	r1, r1, #1
.L92:
	.loc 2 154 0 is_stmt 0 discriminator 1
	cmp	r1, ip
	bcc	.L93
	b	.L94
.L95:
	.loc 2 156 0 is_stmt 1
	mov	r0, #1
.LVL89:
	.loc 2 162 0
	ldmfd	sp, {fp, sp, pc}
.L97:
	.align	2
.L96:
	.word	.LANCHOR0
.LFE182:
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
.LFB183:
	.loc 2 165 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI67:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI68:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI69:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 165 0
.LBB252:
.LBB253:
	.loc 2 89 0
	ldr	r2, .L101
	ldrh	r1, [r2, #194]
.LBE253:
.LBE252:
	.loc 2 168 0
	cmp	r1, #0
.LBB254:
	.loc 2 170 0
	ldrhne	r3, [r2, #190]
	.loc 2 171 0
	subne	r1, r1, #1
	strhne	r1, [r2, #194]	@ movhi
	.loc 2 170 0
	addne	r0, r2, r3, lsl #1
	.loc 2 172 0
	addne	r3, r3, #1
	ubfxne	r3, r3, #0, #10
	.loc 2 170 0
	ldrhne	r0, [r0, #196]
.LBE254:
	.loc 2 166 0
	movweq	r0, #65535
.LBB255:
	.loc 2 172 0
	strhne	r3, [r2, #190]	@ movhi
.LBE255:
	.loc 2 176 0
	ldmfd	sp, {fp, sp, pc}
.L102:
	.align	2
.L101:
	.word	.LANCHOR0
.LFE183:
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
.LFB186:
	.loc 2 217 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL90:
	.movsp ip
	mov	ip, sp
.LCFI70:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI71:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI72:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 217 0
	.loc 2 225 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L104
	.loc 2 230 0
	ldr	r2, .L116
	mov	lr, #6
	mul	lr, lr, r0
	.loc 2 231 0
	mvn	ip, #0
	.loc 2 230 0
	ldr	r5, [r2, #2252]
	mov	r4, r2
	add	r1, r5, lr
.LVL91:
	.loc 2 231 0
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r5, lr]	@ movhi
	.loc 2 233 0
	ldr	r3, [r2, #2256]
.LVL92:
	cmp	r3, #0
	.loc 2 234 0
	streq	r1, [r2, #2256]
	.loc 2 233 0
	beq	.L104
	.loc 2 238 0
	ldr	r7, [r2, #2260]
	lsl	r9, r0, #1
	.loc 2 239 0
	ldrh	r2, [r1, #4]
	.loc 2 238 0
	ldrh	r6, [r7, r9]
.LVL93:
	.loc 2 240 0
	cmp	r2, #0
	.loc 2 239 0
	mulne	ip, r2, r6
.LVL94:
	.loc 2 244 0
	ldr	r6, [r4, #2252]
.LVL95:
	sub	r2, r3, r6
	asr	r8, r2, #1
	ldr	r2, .L116+4
	mul	r2, r2, r8
	.loc 2 252 0
	ldr	r8, [r4, #2248]
	add	r4, r8, r9
	.loc 2 244 0
	uxth	r2, r2
	.loc 2 252 0
	str	r4, [fp, #-48]
.LVL96:
.L112:
	.loc 2 246 0
	lsl	r9, r2, #1
	.loc 2 247 0
	ldrh	r4, [r3, #4]
	.loc 2 246 0
	ldrh	r10, [r7, r9]
	.loc 2 248 0
	cmp	r4, #0
	.loc 2 249 0
	mvneq	r4, #0
.LVL97:
	.loc 2 247 0
	mulne	r4, r4, r10
.LVL98:
	.loc 2 251 0
	cmp	ip, r4
	bne	.L108
	.loc 2 252 0
	ldr	r4, [fp, #-48]
.LVL99:
	ldrh	r9, [r8, r9]
	ldrh	r4, [r4]
	cmp	r9, r4
	bcc	.L110
.LVL100:
.L109:
	.loc 2 270 0
	strh	r2, [r5, lr]	@ movhi
	.loc 2 273 0
	ldr	ip, .L116
.LVL101:
	.loc 2 271 0
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	.loc 2 273 0
	ldr	r2, [ip, #2256]
	cmp	r3, r2
	.loc 2 277 0
	ldrhne	lr, [r3, #2]
	movne	r2, #6
	ldrne	r1, [ip, #2252]
.LVL102:
	.loc 2 274 0
	strheq	r0, [r3, #2]	@ movhi
.LVL103:
	.loc 2 275 0
	streq	r1, [ip, #2256]
	.loc 2 277 0
	mulne	r2, r2, lr
	strhne	r0, [r1, r2]	@ movhi
.LVL104:
	.loc 2 278 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L104
.LVL105:
.L108:
	.loc 2 256 0
	bcc	.L109
.L110:
	.loc 2 259 0
	ldrh	r4, [r3]
.LVL106:
	movw	r9, #65535
	cmp	r4, r9
	bne	.L111
	.loc 2 260 0
	strh	r2, [r1, #2]	@ movhi
	.loc 2 261 0
	strh	r0, [r3]	@ movhi
	.loc 2 262 0
	ldr	r3, .L116
.LVL107:
	str	r1, [r3, #2264]
.LVL108:
.L104:
	.loc 2 281 0
	mov	r0, #0
.LVL109:
	sub	sp, fp, #40
.LCFI73:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL110:
.L111:
.LCFI74:
	.loc 2 267 0
	mov	r3, #6
.LVL111:
	.loc 2 246 0
	mov	r2, r4
	.loc 2 267 0
	mla	r3, r3, r4, r6
.LVL112:
	.loc 2 246 0
	b	.L112
.L117:
	.align	2
.L116:
	.word	.LANCHOR0
	.word	-1431655765
.LFE186:
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
.LFB185:
	.loc 2 186 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL113:
	.movsp ip
	mov	ip, sp
.LCFI75:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI76:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI77:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 186 0
	.loc 2 187 0
	bl	insert_data_list
.LVL114:
	.loc 2 188 0
	ldr	r2, .L120
	movw	r1, #2268
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	.loc 2 189 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
.LBB258:
.LBB259:
	mov	r2, #189
	ldr	r1, .L120+4
	ldr	r0, .L120+8
	bl	printk
.LVL115:
	ldmfd	sp, {fp, sp, pc}
.L121:
	.align	2
.L120:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC1
.LBE259:
.LBE258:
.LFE185:
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
.LFB187:
	.loc 2 284 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL116:
	.movsp ip
	mov	ip, sp
.LCFI78:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI79:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI80:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 284 0
	.loc 2 290 0
	movw	r4, #65535
	cmp	r0, r4
	beq	.L123
	.loc 2 293 0
	ldr	r2, .L129
	mov	lr, #6
	mul	r5, lr, r0
	.loc 2 294 0
	mvn	r3, #0
	.loc 2 293 0
	ldr	r6, [r2, #2252]
	mov	r1, r2
	add	ip, r6, r5
.LVL117:
	.loc 2 294 0
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	.loc 2 296 0
	ldr	r3, [r2, #2272]
.LVL118:
	cmp	r3, #0
	.loc 2 297 0
	streq	ip, [r2, #2272]
	.loc 2 296 0
	beq	.L123
	.loc 2 301 0
	ldr	r9, [r2, #2248]
	lsl	r2, r0, #1
	.loc 2 303 0
	ldr	r8, [r1, #2252]
	.loc 2 301 0
	ldrh	r10, [r9, r2]
.LVL119:
	.loc 2 303 0
	sub	r2, r3, r8
	asr	r7, r2, #1
	ldr	r2, .L129+4
	mul	r2, r2, r7
	uxth	r2, r2
.L127:
	.loc 2 306 0
	lsl	r7, r2, #1
	.loc 2 307 0
	ldrh	r7, [r9, r7]
	cmp	r7, r10
	bcs	.L125
	.loc 2 310 0
	ldrh	r7, [r3]
	cmp	r7, r4
	bne	.L126
	.loc 2 311 0
	strh	r2, [ip, #2]	@ movhi
	.loc 2 312 0
	strh	r0, [r3]	@ movhi
.L123:
	.loc 2 330 0
	mov	r0, #0
.LVL120:
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL121:
.L126:
	.loc 2 316 0
	mla	r3, lr, r7, r8
	.loc 2 306 0
	mov	r2, r7
	b	.L127
.L125:
	.loc 2 319 0
	ldrh	lr, [r3, #2]
	strh	lr, [ip, #2]	@ movhi
	.loc 2 320 0
	strh	r2, [r6, r5]	@ movhi
	.loc 2 322 0
	ldr	r2, [r1, #2272]
	cmp	r3, r2
	.loc 2 326 0
	ldrhne	ip, [r3, #2]
.LVL122:
	movne	r2, #6
	ldrne	r1, [r1, #2252]
	.loc 2 323 0
	strheq	r0, [r3, #2]	@ movhi
.LVL123:
	.loc 2 324 0
	streq	ip, [r1, #2272]
	.loc 2 326 0
	mulne	r2, r2, ip
.LVL124:
	strhne	r0, [r1, r2]	@ movhi
	.loc 2 327 0
	strhne	r0, [r3, #2]	@ movhi
	b	.L123
.L130:
	.align	2
.L129:
	.word	.LANCHOR0
	.word	-1431655765
.LFE187:
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
.LFB184:
	.loc 2 179 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL125:
	.movsp ip
	mov	ip, sp
.LCFI81:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI82:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI83:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 179 0
	.loc 2 180 0
	bl	insert_free_list
.LVL126:
	.loc 2 181 0
	ldr	r2, .L133
	movw	r1, #2276
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	.loc 2 182 0
	ldrh	r2, [r2, #16]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
.LBB262:
.LBB263:
	mov	r2, #182
	ldr	r1, .L133+4
	ldr	r0, .L133+8
	bl	printk
.LVL127:
	ldmfd	sp, {fp, sp, pc}
.L134:
	.align	2
.L133:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC1
.LBE263:
.LBE262:
.LFE184:
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
.LFB188:
	.loc 2 333 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL128:
	.movsp ip
	mov	ip, sp
.LCFI84:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI85:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI86:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 333 0
	.loc 2 336 0
	mov	r5, #6
	.loc 2 337 0
	movw	r3, #65535
	.loc 2 336 0
	ldr	r7, .L142
	mul	r5, r5, r1
	.loc 2 333 0
	mov	r8, r0
	.loc 2 336 0
	ldr	r6, [r7, #2252]
	add	r4, r6, r5
.LVL129:
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
	ldr	r1, .L142+4
.LVL130:
	ldr	r0, .L142+8
	bl	printk
.LVL131:
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
	ldrne	r2, [r7, #2252]
	movne	r1, #6
.LVL132:
	.loc 2 341 0
	moveq	r3, #0
	streq	r3, [r8]
	.loc 2 343 0
	mlane	r3, r1, r3, r2
	.loc 2 344 0
	mvnne	r2, #0
	.loc 2 343 0
	strne	r3, [r8]
	.loc 2 344 0
	strhne	r2, [r3, #2]	@ movhi
.L139:
	.loc 2 355 0
	mvn	r3, #0
	.loc 2 357 0
	mov	r0, #0
	.loc 2 355 0
	strh	r3, [r6, r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	.loc 2 357 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL133:
.L137:
	.loc 2 346 0
	cmp	r3, r2
	mov	r2, #6
	.loc 2 351 0
	ldrne	r0, [r7, #2252]
	ldrh	r1, [r4, #2]
.LVL134:
	.loc 2 348 0
	ldreq	r3, [r7, #2252]
	.loc 2 351 0
	mlane	r3, r2, r3, r0
	.loc 2 348 0
	muleq	r2, r2, r1
	mvneq	r1, #0
	.loc 2 351 0
	strhne	r1, [r3, #2]	@ movhi
	.loc 2 352 0
	ldrhne	r0, [r4, #2]
	.loc 2 353 0
	ldrhne	r1, [r6, r5]
	ldrne	r3, [r7, #2252]
	mulne	r2, r2, r0
	strh	r1, [r3, r2]	@ movhi
	b	.L139
.L143:
	.align	2
.L142:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC1
.LFE188:
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
.LFB189:
	.loc 2 360 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL135:
	.movsp ip
	mov	ip, sp
.LCFI87:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI88:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI89:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 360 0
	.loc 2 364 0
	ldr	r3, [r0]
.LVL136:
	cmp	r3, #0
	beq	.L150
	.loc 2 369 0
	ldr	r2, .L151
	.loc 2 368 0
	movw	lr, #65535
	.loc 2 369 0
	mov	r4, #6
	ldr	r2, [r2, #2252]
.L146:
	.loc 2 368 0
	cmp	r1, #0
	bne	.L147
.L149:
	.loc 2 372 0
	ldr	r4, .L151+4
	sub	r3, r3, r2
.LVL137:
	asr	r3, r3, #1
	mul	r4, r4, r3
	.loc 2 373 0
	uxth	r1, r4
.LVL138:
	bl	List_remove_node
.LVL139:
	uxth	r0, r4
	.loc 2 375 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.LVL140:
.L147:
	.loc 2 368 0 discriminator 1
	ldrh	ip, [r3]
	cmp	ip, lr
	beq	.L149
	.loc 2 370 0
	sub	r1, r1, #1
.LVL141:
	.loc 2 369 0
	mla	r3, r4, ip, r2
	.loc 2 370 0
	uxth	r1, r1
.LVL142:
	b	.L146
.L150:
	.loc 2 365 0
	movw	r0, #65535
.LVL143:
	.loc 2 376 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L152:
	.align	2
.L151:
	.word	.LANCHOR0
	.word	-1431655765
.LFE189:
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_head_node, %function
List_pop_head_node:
	.fnstart
.LFB190:
	.loc 2 379 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL144:
	.movsp ip
	mov	ip, sp
.LCFI90:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI91:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI92:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 379 0
	.loc 2 380 0
	mov	r1, #0
	bl	List_pop_index_node
.LVL145:
	.loc 2 381 0
	ldmfd	sp, {fp, sp, pc}
.LFE190:
	.fnend
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
.LFB191:
	.loc 2 384 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL146:
	.movsp ip
	mov	ip, sp
.LCFI93:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI94:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI95:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 384 0
	.loc 2 388 0
	ldr	r2, .L160
	ldr	r3, [r2, #2256]
.LVL147:
	cmp	r3, #0
	.loc 2 393 0
	ldrne	r1, [r2, #2252]
	movne	ip, #6
	.loc 2 392 0
	movwne	r2, #65535
	.loc 2 388 0
	bne	.L156
.L159:
	.loc 2 389 0
	movw	r0, #65535
.LVL148:
	ldmfd	sp, {fp, sp, pc}
.LVL149:
.L158:
	.loc 2 394 0
	sub	r0, r0, #1
.LVL150:
	.loc 2 393 0
	mla	r3, ip, r3, r1
.LVL151:
	.loc 2 394 0
	uxth	r0, r0
.LVL152:
.L156:
	.loc 2 392 0
	cmp	r0, #0
	beq	.L157
	.loc 2 392 0 is_stmt 0 discriminator 1
	ldrh	r3, [r3]
.LVL153:
	cmp	r3, r2
	bne	.L158
	b	.L159
.LVL154:
.L157:
	.loc 2 398 0 is_stmt 1
	ldr	r0, .L160+4
.LVL155:
	sub	r3, r3, r1
.LVL156:
	asr	r3, r3, #1
	mul	r3, r0, r3
	uxth	r0, r3
	.loc 2 401 0
	ldmfd	sp, {fp, sp, pc}
.L161:
	.align	2
.L160:
	.word	.LANCHOR0
	.word	-1431655765
.LFE191:
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
.LFB192:
	.loc 2 404 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL157:
	.movsp ip
	mov	ip, sp
.LCFI96:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI97:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI98:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 404 0
	.loc 2 413 0
	movw	r3, #2280
	.loc 2 404 0
	mov	r5, r0
	.loc 2 413 0
	ldr	r4, .L170
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L164
	.loc 2 413 0 is_stmt 0 discriminator 1
	movw	r3, #2328
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L164
	.loc 2 413 0 discriminator 2
	movw	r3, #2376
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L164
.LBB266:
.LBB267:
	.loc 2 416 0 is_stmt 1
	mov	r3, #6
	ldr	r1, [r4, #2252]
	mul	r3, r3, r0
	.loc 2 418 0
	ldr	r2, [r4, #2256]
	.loc 2 416 0
	add	r7, r1, r3
.LVL158:
	.loc 2 418 0
	cmp	r7, r2
	beq	.L164
	.loc 2 421 0
	ldr	r0, [r4, #2260]
	lsl	r2, r5, #1
	.loc 2 422 0
	ldrh	r6, [r0, r2]
	.loc 2 426 0
	movw	r0, #65535
	.loc 2 422 0
	ldrh	r2, [r7, #4]
	.loc 2 424 0
	muls	r6, r2, r6
.LVL159:
	.loc 2 426 0
	ldrh	r2, [r7, #2]
	.loc 2 424 0
	mvneq	r6, #0
	.loc 2 426 0
	cmp	r2, r0
	bne	.L167
	ldrh	r3, [r1, r3]
	cmp	r3, r2
	bne	.L167
	movw	r2, #426
	ldr	r1, .L170+4
	ldr	r0, .L170+8
	bl	printk
.LVL160:
.L167:
	.loc 2 427 0
	ldrh	r3, [r7, #2]
	mov	r2, #6
	mul	r2, r2, r3
	.loc 2 428 0
	ldr	r3, .L170+12
	asr	r1, r2, #1
	mul	r3, r3, r1
	.loc 2 429 0
	ldr	r1, [r4, #2260]
	lsl	r3, r3, #1
	.loc 2 430 0
	ldrh	r3, [r1, r3]
	ldr	r1, [r4, #2252]
	add	r2, r1, r2
	ldrh	r2, [r2, #4]
	.loc 2 432 0
	muls	r3, r2, r3
.LVL161:
	mvneq	r3, #0
	.loc 2 434 0
	cmp	r6, r3
	bcs	.L164
	.loc 2 435 0
	mov	r1, r5
	ldr	r0, .L170+16
	bl	List_remove_node
.LVL162:
	movw	r3, #2268
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L169
	movw	r2, #435
	ldr	r1, .L170+4
	ldr	r0, .L170+8
	bl	printk
.LVL163:
.L169:
	movw	r2, #2268
	.loc 2 436 0
	mov	r0, r5
	.loc 2 435 0
	ldrh	r3, [r4, r2]
	sub	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
	.loc 2 436 0
	bl	INSERT_DATA_LIST
.LVL164:
.L164:
.LBE267:
.LBE266:
	.loc 2 439 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L171:
	.align	2
.L170:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC1
	.word	-1431655765
	.word	.LANCHOR0+2256
.LFE192:
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
.LFB195:
	.loc 2 516 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL165:
	.movsp ip
	mov	ip, sp
.LCFI99:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI100:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI101:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 516 0
	.loc 2 520 0
	mov	r3, #0
	.loc 2 516 0
	mov	r4, r0
	.loc 2 520 0
	ldrh	r1, [r0, #10]
	ldr	r2, [r0, #12]
.LVL166:
.L173:
	uxth	r5, r3
	.loc 2 520 0 is_stmt 0 discriminator 1
	cmp	r5, r1
	bcc	.L175
.L176:
.LBB270:
.LBB271:
	.loc 2 532 0 is_stmt 1
	mov	r2, #532
	ldr	r1, .L182
	ldr	r0, .L182+4
	bl	printk
.LVL167:
	b	.L177
.L175:
.LBE271:
.LBE270:
	.loc 2 521 0
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L173
	.loc 2 522 0
	bl	FtlFreeSysBlkQueueOut
.LVL168:
	.loc 2 523 0
	cmp	r0, #0
	.loc 2 522 0
	strh	r0, [r7]	@ movhi
	.loc 2 523 0
	beq	.L174
	.loc 2 526 0
	ldr	r3, [r4, #28]
	.loc 2 524 0
	strh	r6, [r4, #2]	@ movhi
	.loc 2 525 0
	strh	r5, [r4]	@ movhi
	.loc 2 526 0
	add	r3, r3, #1
	str	r3, [r4, #28]
	.loc 2 527 0
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L174:
	.loc 2 532 0
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bls	.L176
.L177:
	.loc 2 534 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L183:
	.align	2
.L182:
	.word	.LANCHOR1+73
	.word	.LC1
.LFE195:
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
.LFB200:
	.loc 2 700 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI102:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI103:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI104:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 700 0
	.loc 2 705 0
	mov	r1, #0
	.loc 2 706 0
	movw	ip, #65535
	.loc 2 705 0
	ldr	r0, .L194
	ldrh	r2, [r0, #114]
	mov	lr, r0
	.loc 2 706 0
	ldr	r3, [r0, #2424]
	mov	r0, #12
.L185:
	uxth	r4, r1
	.loc 2 705 0 discriminator 1
	cmp	r4, r2
	bcc	.L187
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	.loc 2 716 0
	mov	r6, #12
.L188:
	uxth	r5, r1
	.loc 2 715 0 discriminator 1
	cmp	r5, r2
	bcc	.L190
	.loc 2 724 0
	cmp	r4, r2
	bcc	.L186
	.loc 2 730 0
	movw	r1, #2428
	mov	r4, r2
	ldrh	r5, [lr, r1]
	mvn	r0, #0
	mov	r1, #0
.L191:
	uxth	ip, r1
	.loc 2 729 0 discriminator 1
	cmp	ip, r2
	bcc	.L193
	.loc 2 735 0
	cmp	r4, r2
	bcc	.L186
	.loc 2 735 0 is_stmt 0 discriminator 1
	movw	r2, #735
	ldr	r1, .L194+4
	ldr	r0, .L194+8
	bl	printk
.LVL169:
	b	.L186
.L187:
	add	r1, r1, #1
	.loc 2 706 0 is_stmt 1
	mla	r5, r0, r1, r3
	ldrh	r5, [r5, #-12]
	cmp	r5, ip
	bne	.L185
.L186:
	.loc 2 737 0
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L190:
	.loc 2 716 0
	mla	ip, r6, r1, r3
	add	r1, r1, #1
	ldr	ip, [ip, #4]
	.loc 2 717 0
	cmp	r0, ip
	movls	r7, #0
	movhi	r7, #1
	cmp	ip, #0
	movlt	r7, #0
	cmp	r7, #0
	movne	r0, ip
	movne	r4, r5
	b	.L188
.L193:
	.loc 2 730 0
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L192
	.loc 2 730 0 is_stmt 0 discriminator 1
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L192:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L191
.L195:
	.align	2
.L194:
	.word	.LANCHOR0
	.word	.LANCHOR1+99
	.word	.LC1
.LFE200:
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
.LFB206:
	.loc 2 882 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL170:
	.movsp ip
	mov	ip, sp
.LCFI105:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI106:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI107:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 882 0
	.loc 2 885 0
	movw	r1, #2430
	ldr	ip, .L202
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L197
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	.loc 2 894 0
	ldmfd	sp, {fp, sp, pc}
.L197:
	.loc 2 886 0
	mov	r2, #0
.LBB274:
.LBB275:
	.loc 2 890 0
	movw	lr, #65535
.LBE275:
.LBE274:
	.loc 2 886 0
	strh	r2, [ip, r1]	@ movhi
	.loc 2 887 0
	str	r2, [ip, #2432]
	ldrh	r1, [ip, #16]
.LBB277:
.LBB276:
	.loc 2 890 0
	ldr	r2, [ip, #2260]
	add	r1, r2, r1, lsl #1
.L198:
	.loc 2 888 0
	cmp	r2, r1
	bne	.L200
	ldmfd	sp, {fp, sp, pc}
.L200:
	.loc 2 890 0
	ldrh	ip, [r2], #2
	cmp	ip, lr
	.loc 2 891 0
	ldrne	r0, [r3, #2432]
.LVL171:
	addne	r0, r0, ip
	strne	r0, [r3, #2432]
	b	.L198
.L203:
	.align	2
.L202:
	.word	.LANCHOR0
.LBE276:
.LBE277:
.LFE206:
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
.LFB212:
	.loc 2 1347 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL172:
	.movsp ip
	mov	ip, sp
.LCFI108:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI109:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI110:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1347 0
	.loc 2 1348 0
	cmp	r1, #0
	.loc 2 1347 0
	mov	r3, r0
	.loc 2 1348 0
	beq	.L205
	bl	ftl_set_blk_mode.part.6
.LVL173:
	ldmfd	sp, {fp, sp, pc}
.LVL174:
.L205:
	.loc 2 1351 0
	ldr	r2, .L207
	lsr	r0, r0, #5
	and	r3, r3, #31
.LVL175:
	mov	ip, #1
	ldr	r1, [r2]
.LVL176:
	ldr	r2, [r1, r0, lsl #2]
	bic	r3, r2, ip, lsl r3
	str	r3, [r1, r0, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L208:
	.align	2
.L207:
	.word	.LANCHOR0
.LFE212:
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
.LFB213:
	.loc 2 1355 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL177:
	.movsp ip
	mov	ip, sp
.LCFI111:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI112:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI113:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1355 0
	.loc 2 1356 0
	lsr	r2, r0, #5
	and	r0, r0, #31
.LVL178:
	ldr	r3, .L210
	ldr	r3, [r3]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	.loc 2 1357 0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L211:
	.align	2
.L210:
	.word	.LANCHOR0
.LFE213:
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
.LFB219:
	.loc 2 1608 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL179:
	.movsp ip
	mov	ip, sp
.LCFI114:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI115:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI116:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1608 0
	.loc 2 1610 0
	mov	r3, #0
	add	ip, r0, r2, lsl #1
	strh	r3, [r0, #4]	@ movhi
	.loc 2 1613 0
	movw	r5, #65535
	.loc 2 1611 0
	ldr	r3, .L219
	add	ip, ip, #14
	ldrh	lr, [r3, #8]
.L213:
	.loc 2 1611 0 is_stmt 0 discriminator 1
	cmp	r2, lr
	bcc	.L215
	ldrh	r3, [r3, #78]
	add	ip, r0, #16
	.loc 2 1620 0 is_stmt 1
	movw	r4, #65535
	sub	r3, r3, #1
	sub	r1, r3, r1
.LVL180:
	.loc 2 1622 0
	mov	r3, #0
	uxth	r1, r1
.L216:
	.loc 2 1618 0 discriminator 1
	uxth	r2, r3
.LVL181:
	cmp	lr, r2
	bhi	.L218
	.loc 2 1625 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.LVL182:
.L215:
	.loc 2 1613 0
	ldrh	r4, [ip, #2]!
	.loc 2 1611 0
	add	r2, r2, #1
.LVL183:
	uxth	r2, r2
.LVL184:
	.loc 2 1613 0
	cmp	r4, r5
	.loc 2 1615 0
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L213
.LVL185:
.L218:
	.loc 2 1620 0
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	.loc 2 1622 0
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L216
.L220:
	.align	2
.L219:
	.word	.LANCHOR0
.LFE219:
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
.LFB222:
	.loc 2 2013 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL186:
	.movsp ip
	mov	ip, sp
.LCFI117:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI118:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI119:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2013 0
	.loc 2 2016 0
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	ldmfdeq	sp, {fp, sp, pc}
	.loc 2 2018 0
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	ldmfdeq	sp, {fp, sp, pc}
.LBB280:
.LBB281:
	.loc 2 2020 0
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	.loc 2 2025 0
	mov	lr, #0
	.loc 2 2020 0
	add	r2, r0, r2, lsl #1
	ldrh	r1, [r2, #16]
.LVL187:
	.loc 2 2023 0
	ldr	r2, .L227
	ldrh	ip, [r2, #8]
	.loc 2 2021 0
	mov	r2, r3
.L224:
	cmp	r1, r2
	beq	.L226
	ldmfd	sp, {fp, sp, pc}
.L226:
	.loc 2 2022 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2023 0
	cmp	r3, ip
	.loc 2 2022 0
	strb	r3, [r0, #6]
	.loc 2 2024 0
	ldrheq	r3, [r0, #2]
	.loc 2 2025 0
	strbeq	lr, [r0, #6]
	.loc 2 2024 0
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	.loc 2 2027 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r0, r3, lsl #1
	ldrh	r1, [r3, #16]
	b	.L224
.L228:
	.align	2
.L227:
	.word	.LANCHOR0
.LBE281:
.LBE280:
.LFE222:
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
.LFB224:
	.loc 2 2045 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL188:
	.movsp ip
	mov	ip, sp
.LCFI120:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI121:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI122:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2045 0
	.loc 2 2048 0
	ldr	r6, .L234
	.loc 2 2045 0
	mov	r4, r0
	.loc 2 2048 0
	ldrh	r2, [r0]
	ldrh	r3, [r6, #16]
	cmp	r2, r3
	bcc	.L230
	.loc 2 2048 0 is_stmt 0 discriminator 1
	mov	r2, #2048
	ldr	r1, .L234+4
	ldr	r0, .L234+8
	bl	printk
.LVL189:
.L230:
	.loc 2 2051 0 is_stmt 1
	ldrh	r9, [r6, #8]
	add	r7, r4, #16
	.loc 2 2052 0
	ldr	r10, .L234+12
	.loc 2 2053 0
	mvn	r8, #0
	.loc 2 2049 0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	.loc 2 2050 0
	strb	r5, [r4, #7]
.L231:
	.loc 2 2051 0 discriminator 1
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L233
	.loc 2 2061 0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	.loc 2 2064 0
	mov	r0, #0
	.loc 2 2061 0
	ldrh	r2, [r6, #78]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2062 0
	mov	r3, #1
	strb	r3, [r4, #9]
	.loc 2 2064 0
	sub	sp, fp, #40
.LCFI123:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL190:
.L233:
.LCFI124:
	.loc 2 2052 0
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
.LVL191:
	.loc 2 2053 0
	strh	r8, [r7, #-2]	@ movhi
	.loc 2 2052 0
	str	r0, [fp, #-48]
	.loc 2 2054 0
	bl	FtlBbmIsBadBlock
.LVL192:
	cmp	r0, #0
	.loc 2 2055 0
	ldreq	r3, [fp, #-48]
	strheq	r3, [r7, #-2]	@ movhi
	.loc 2 2056 0
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L231
.L235:
	.align	2
.L234:
	.word	.LANCHOR0
	.word	.LANCHOR1+121
	.word	.LC1
	.word	.LANCHOR0+36
.LFE224:
	.fnend
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
.LFB233:
	.loc 2 2401 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL193:
	.movsp ip
	mov	ip, sp
.LCFI125:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI126:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI127:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2401 0
	.loc 2 2405 0
	mov	r5, #0
	.loc 2 2401 0
	mov	r7, r0
	.loc 2 2405 0
	ldr	r3, .L242
	.loc 2 2403 0
	mov	r4, r5
	.loc 2 2405 0
	ldrh	r8, [r3, #8]
	mov	r6, r3
	.loc 2 2408 0
	ldrh	r9, [r3, #78]
	.loc 2 2406 0
	add	r10, r3, #36
.L237:
	.loc 2 2405 0 discriminator 1
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L239
	.loc 2 2410 0
	cmp	r4, #0
	moveq	r0, r4
	beq	.L240
	.loc 2 2411 0
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.LVL194:
.L240:
	.loc 2 2412 0
	ldr	r1, [r6, #2252]
	mov	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	.loc 2 2414 0
	mov	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL195:
.L239:
	.loc 2 2406 0
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
.LVL196:
	.loc 2 2407 0
	bl	FtlBbmIsBadBlock
.LVL197:
	cmp	r0, #0
	add	r5, r5, #1
	.loc 2 2408 0
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L237
.L243:
	.align	2
.L242:
	.word	.LANCHOR0
.LFE233:
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
.LFB234:
	.loc 2 2417 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI128:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI129:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI130:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2417 0
	.loc 2 2420 0
	ldr	r2, .L247
	ldr	r0, [r2, #2272]
	cmp	r0, #0
	ldmfdeq	sp, {fp, sp, pc}
	.loc 2 2421 0
	ldr	r3, [r2, #2252]
	sub	r0, r0, r3
	ldr	r3, .L247+4
	asr	r0, r0, #1
	mul	r0, r3, r0
	ldr	r3, [r2, #2248]
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r0, [r3, r0]
	.loc 2 2423 0
	ldmfd	sp, {fp, sp, pc}
.L248:
	.align	2
.L247:
	.word	.LANCHOR0
	.word	-1431655765
.LFE234:
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
.LFB235:
	.loc 2 2426 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL198:
	.movsp ip
	mov	ip, sp
.LCFI131:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI132:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI133:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2426 0
	.loc 2 2431 0
	ldr	r1, .L257
	ldr	r3, [r1, #2272]
	cmp	r3, #0
	beq	.L255
	.loc 2 2432 0
	movw	r2, #2276
	.loc 2 2434 0
	ldr	ip, [r1, #2252]
	.loc 2 2432 0
	ldrh	r2, [r1, r2]
	.loc 2 2436 0
	mov	r4, #6
	movw	r5, #65535
	.loc 2 2434 0
	sub	r3, r3, ip
	asr	r3, r3, #1
	.loc 2 2432 0
	rsb	r2, r2, r2, lsl #3
	asr	r2, r2, #3
	cmp	r0, r2
	.loc 2 2433 0
	uxthgt	r0, r2
	.loc 2 2434 0
	ldr	r2, .L257+4
	mul	r3, r2, r3
	.loc 2 2435 0
	mov	r2, #0
	.loc 2 2434 0
	uxth	r3, r3
.L252:
	.loc 2 2435 0 discriminator 1
	uxth	lr, r2
	cmp	r0, lr
	bls	.L254
	.loc 2 2436 0
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L256
.L254:
	.loc 2 2440 0
	ldr	r2, [r1, #2248]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
.LVL199:
	ldmfd	sp, {r4, r5, fp, sp, pc}
.LVL200:
.L256:
	mov	r3, lr
	b	.L252
.L255:
	.loc 2 2427 0
	mov	r0, r3
.LVL201:
	.loc 2 2443 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L258:
	.align	2
.L257:
	.word	.LANCHOR0
	.word	-1431655765
.LFE235:
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
.LFB140:
	.loc 3 11 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL202:
	.movsp ip
	mov	ip, sp
.LCFI134:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI135:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI136:
	.pad #20
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 11 0
	.loc 3 17 0
	ldr	r6, .L271
	.loc 3 11 0
	mov	r7, r0
	.loc 3 15 0
	ldr	r1, .L271+4
	add	r5, r7, #12
	bl	strcpy
.LVL203:
	.loc 3 17 0
	ldr	r2, [r6, #92]
	mov	r0, r5
	ldr	r1, .L271+8
	.loc 3 32 0
	movw	r9, #2276
	.loc 3 17 0
	bl	sprintf
.LVL204:
	add	r5, r5, r0
	.loc 3 18 0
	ldr	r1, .L271+12
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
.LVL205:
	.loc 3 19 0
	ldr	r2, [r6, #2436]
	mov	r0, r5
	ldr	r1, .L271+16
	bl	sprintf
.LVL206:
	add	r5, r5, r0
	.loc 3 20 0
	ldr	r2, [r6, #2432]
	ldr	r1, .L271+20
	mov	r0, r5
	bl	sprintf
.LVL207:
	add	r5, r5, r0
	.loc 3 21 0
	ldr	r2, [r6, #2440]
	ldr	r1, .L271+24
	mov	r0, r5
	bl	sprintf
.LVL208:
	add	r5, r5, r0
	.loc 3 22 0
	ldr	r2, [r6, #2444]
	ldr	r1, .L271+28
	mov	r0, r5
	bl	sprintf
.LVL209:
	add	r5, r5, r0
	.loc 3 23 0
	ldr	r2, [r6, #2448]
	ldr	r1, .L271+32
	mov	r0, r5
	bl	sprintf
.LVL210:
	add	r5, r5, r0
	.loc 3 24 0
	ldr	r2, [r6, #2452]
	ldr	r1, .L271+36
	mov	r0, r5
	bl	sprintf
.LVL211:
	add	r5, r5, r0
	.loc 3 25 0
	ldr	r2, [r6, #2456]
	ldr	r1, .L271+40
	mov	r0, r5
	bl	sprintf
.LVL212:
	add	r5, r5, r0
	.loc 3 26 0
	ldr	r2, [r6, #2460]
	ldr	r1, .L271+44
	mov	r0, r5
	bl	sprintf
.LVL213:
	.loc 3 27 0
	ldr	r2, [r6, #2464]
	.loc 3 26 0
	add	r5, r5, r0
	.loc 3 27 0
	ldr	r1, .L271+48
	mov	r0, r5
	.loc 3 39 0
	ldr	r4, .L271+52
	.loc 3 50 0
	ldr	r8, .L271+56
	.loc 3 27 0
	lsr	r2, r2, #11
	bl	sprintf
.LVL214:
	.loc 3 28 0
	ldr	r2, [r6, #2468]
	.loc 3 27 0
	add	r5, r5, r0
	.loc 3 28 0
	ldr	r1, .L271+60
	mov	r0, r5
	lsr	r2, r2, #11
	bl	sprintf
.LVL215:
	add	r5, r5, r0
	.loc 3 29 0
	ldr	r2, [r6, #2472]
	ldr	r1, .L271+64
	mov	r0, r5
	bl	sprintf
.LVL216:
	add	r5, r5, r0
	.loc 3 30 0
	ldr	r2, [r6, #2476]
	ldr	r1, .L271+68
	mov	r0, r5
	bl	sprintf
.LVL217:
	add	r5, r5, r0
	.loc 3 31 0
	ldrh	r2, [r6, #134]
	ldr	r1, .L271+72
	mov	r0, r5
	bl	sprintf
.LVL218:
	add	r5, r5, r0
	.loc 3 32 0
	ldrh	r2, [r6, r9]
	ldr	r1, .L271+76
	mov	r0, r5
	bl	sprintf
.LVL219:
	add	r5, r5, r0
	.loc 3 33 0
	ldr	r2, [r6, #2480]
	ldr	r1, .L271+80
	mov	r0, r5
	bl	sprintf
.LVL220:
	add	r5, r5, r0
	.loc 3 34 0
	ldr	r2, [r6, #2484]
	ldr	r1, .L271+84
	mov	r0, r5
	bl	sprintf
.LVL221:
	add	r5, r5, r0
	.loc 3 35 0
	ldr	r2, [r6, #2488]
	ldr	r1, .L271+88
	mov	r0, r5
	bl	sprintf
.LVL222:
	add	r5, r5, r0
	.loc 3 36 0
	ldr	r2, [r6, #2492]
	ldr	r1, .L271+92
	mov	r0, r5
	bl	sprintf
.LVL223:
	add	r5, r5, r0
	.loc 3 37 0
	ldr	r2, [r6, #2496]
	ldr	r1, .L271+96
	mov	r0, r5
	bl	sprintf
.LVL224:
	add	r5, r5, r0
	.loc 3 38 0
	ldr	r2, [r6, #2500]
	ldr	r1, .L271+100
	mov	r0, r5
	bl	sprintf
.LVL225:
	add	r5, r5, r0
	.loc 3 39 0
	ldrh	r2, [r4, #30]
	ldr	r1, .L271+104
	mov	r0, r5
	bl	sprintf
.LVL226:
	add	r5, r5, r0
	.loc 3 40 0
	ldrh	r2, [r4, #28]
	ldr	r1, .L271+108
	mov	r0, r5
	bl	sprintf
.LVL227:
	add	r5, r5, r0
	.loc 3 41 0
	ldr	r2, [r6, #116]
	ldr	r1, .L271+112
	mov	r0, r5
	bl	sprintf
.LVL228:
	add	r5, r5, r0
	.loc 3 42 0
	ldr	r2, [r6, #108]
	ldr	r1, .L271+116
	mov	r0, r5
	bl	sprintf
.LVL229:
	add	r5, r5, r0
	.loc 3 43 0
	ldr	r2, [r6, #4]
	ldr	r1, .L271+120
	mov	r0, r5
	bl	sprintf
.LVL230:
	add	r5, r5, r0
	.loc 3 44 0
	ldrh	r2, [r6, #194]
	ldr	r1, .L271+124
	mov	r0, r5
	bl	sprintf
.LVL231:
	add	r5, r5, r0
	.loc 3 45 0
	ldrh	r2, [r6, #16]
	ldr	r1, .L271+128
	mov	r0, r5
	bl	sprintf
.LVL232:
	.loc 3 46 0
	movw	r3, #2552
	.loc 3 45 0
	add	r5, r5, r0
	.loc 3 46 0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L271+132
	.loc 3 50 0
	movw	r4, #2280
	.loc 3 46 0
	bl	sprintf
.LVL233:
	add	r5, r5, r0
	.loc 3 47 0
	ldr	r2, [r6, #20]
	ldr	r1, .L271+136
	mov	r0, r5
	bl	sprintf
.LVL234:
	.loc 3 48 0
	movw	r3, #2556
	.loc 3 47 0
	add	r5, r5, r0
	.loc 3 48 0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L271+140
	bl	sprintf
.LVL235:
	add	r5, r5, r0
	.loc 3 49 0
	ldrh	r2, [r6, #128]
	ldr	r1, .L271+144
	mov	r0, r5
	bl	sprintf
.LVL236:
	add	r5, r5, r0
	.loc 3 50 0
	ldrh	r2, [r8, #2]
	ldr	r1, .L271+148
	mov	r0, r5
	bl	sprintf
.LVL237:
	add	r5, r5, r0
	.loc 3 51 0
	ldrb	r2, [r6, #2286]	@ zero_extendqisi2
	ldr	r1, .L271+152
	mov	r0, r5
	bl	sprintf
.LVL238:
	add	r5, r5, r0
	.loc 3 52 0
	ldrh	r2, [r6, r4]
	ldr	r1, .L271+156
	mov	r0, r5
	bl	sprintf
.LVL239:
	add	r5, r5, r0
	.loc 3 53 0
	ldrb	r2, [r6, #2288]	@ zero_extendqisi2
	ldr	r1, .L271+160
	mov	r0, r5
	bl	sprintf
.LVL240:
	add	r5, r5, r0
	.loc 3 54 0
	ldrh	r2, [r8, #4]
	ldr	r1, .L271+164
	mov	r0, r5
	bl	sprintf
.LVL241:
	.loc 3 55 0
	ldrh	r3, [r6, r4]
	.loc 3 54 0
	add	r5, r5, r0
	.loc 3 55 0
	ldr	r2, [r6, #2260]
	mov	r0, r5
	ldr	r1, .L271+168
	.loc 3 56 0
	movw	r4, #2328
	.loc 3 55 0
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
.LVL242:
	add	r5, r5, r0
	.loc 3 56 0
	ldrh	r2, [r8, #50]
	ldr	r1, .L271+172
	mov	r0, r5
	bl	sprintf
.LVL243:
	add	r5, r5, r0
	.loc 3 57 0
	ldrb	r2, [r6, #2334]	@ zero_extendqisi2
	ldr	r1, .L271+176
	mov	r0, r5
	bl	sprintf
.LVL244:
	add	r5, r5, r0
	.loc 3 58 0
	ldrh	r2, [r6, r4]
	ldr	r1, .L271+180
	mov	r0, r5
	bl	sprintf
.LVL245:
	add	r5, r5, r0
	.loc 3 59 0
	ldrb	r2, [r6, #2336]	@ zero_extendqisi2
	ldr	r1, .L271+184
	mov	r0, r5
	bl	sprintf
.LVL246:
	add	r5, r5, r0
	.loc 3 60 0
	ldrh	r2, [r8, #52]
	ldr	r1, .L271+188
	mov	r0, r5
	bl	sprintf
.LVL247:
	.loc 3 61 0
	ldrh	r3, [r6, r4]
	.loc 3 60 0
	add	r5, r5, r0
	.loc 3 61 0
	ldr	r2, [r6, #2260]
	mov	r0, r5
	ldr	r1, .L271+192
	.loc 3 62 0
	add	r4, r8, #96
	.loc 3 61 0
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
.LVL248:
	add	r5, r5, r0
	.loc 3 62 0
	ldrh	r2, [r8, #98]
	ldr	r1, .L271+196
	mov	r0, r5
	bl	sprintf
.LVL249:
	add	r5, r5, r0
	.loc 3 63 0
	ldrb	r2, [r6, #2382]	@ zero_extendqisi2
	ldr	r1, .L271+200
	mov	r0, r5
	bl	sprintf
.LVL250:
	.loc 3 64 0
	movw	r3, #2376
	.loc 3 63 0
	add	r5, r5, r0
	.loc 3 64 0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L271+204
	bl	sprintf
.LVL251:
	add	r5, r5, r0
	.loc 3 65 0
	ldrb	r2, [r6, #2384]	@ zero_extendqisi2
	ldr	r1, .L271+208
	mov	r0, r5
	bl	sprintf
.LVL252:
	add	r5, r5, r0
	.loc 3 66 0
	ldrh	r2, [r8, #100]
	ldr	r1, .L271+212
	mov	r0, r5
	bl	sprintf
.LVL253:
	add	r5, r5, r0
	.loc 3 67 0
	ldrh	r2, [r4, #194]
	ldr	r1, .L271+216
	mov	r0, r5
	bl	sprintf
.LVL254:
	add	r5, r5, r0
	.loc 3 68 0
	ldrb	r2, [r6, #2574]	@ zero_extendqisi2
	ldr	r1, .L271+220
	mov	r0, r5
	bl	sprintf
.LVL255:
	.loc 3 67 0
	movw	r8, #2568
	.loc 3 68 0
	add	r5, r5, r0
	.loc 3 69 0
	ldrh	r2, [r6, r8]
	mov	r0, r5
	ldr	r1, .L271+224
	bl	sprintf
.LVL256:
	add	r5, r5, r0
	.loc 3 70 0
	ldrb	r2, [r6, #2576]	@ zero_extendqisi2
	ldr	r1, .L271+228
	mov	r0, r5
	bl	sprintf
.LVL257:
	add	r5, r5, r0
	.loc 3 71 0
	ldrh	r2, [r4, #196]
	ldr	r1, .L271+232
	mov	r0, r5
	bl	sprintf
.LVL258:
	.loc 3 72 0
	ldr	r3, [r6, #2692]
	.loc 3 71 0
	add	r5, r5, r0
	.loc 3 72 0
	ldr	r2, [r6, #2696]
	mov	r0, r5
	ldr	r1, .L271+236
	str	r3, [sp]
	ldr	r3, [r6, #2700]
	bl	sprintf
.LVL259:
	add	r4, r5, r0
	.loc 3 73 0
	ldr	r2, [r6, #2688]
	ldr	r1, .L271+240
	mov	r0, r4
	bl	sprintf
.LVL260:
	add	r4, r4, r0
	.loc 3 74 0
	ldr	r2, [r6, #2712]
	ldr	r1, .L271+244
	mov	r0, r4
	bl	sprintf
.LVL261:
	.loc 3 75 0
	movw	r3, #3128
	.loc 3 74 0
	add	r4, r4, r0
	.loc 3 75 0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L271+248
	bl	sprintf
.LVL262:
	.loc 3 76 0
	movw	r3, #3130
	.loc 3 75 0
	add	r4, r4, r0
	.loc 3 76 0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L271+252
	bl	sprintf
.LVL263:
	add	r4, r4, r0
	.loc 3 77 0
	ldr	r2, [r6, #3132]
	ldr	r1, .L271+256
	mov	r0, r4
	bl	sprintf
.LVL264:
	.loc 3 78 0
	add	r3, r6, #3136
	.loc 3 77 0
	add	r4, r4, r0
	.loc 3 78 0
	ldrh	r2, [r3]
	mov	r0, r4
	ldr	r1, .L271+260
	bl	sprintf
.LVL265:
	add	r4, r4, r0
	.loc 3 79 0
	bl	GetFreeBlockMinEraseCount
.LVL266:
	ldr	r1, .L271+264
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL267:
	add	r4, r4, r0
	.loc 3 80 0
	ldrh	r0, [r6, r9]
	bl	GetFreeBlockMaxEraseCount
.LVL268:
	ldr	r1, .L271+268
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
.LVL269:
	.loc 3 81 0
	ldr	r3, .L271+272
	.loc 3 80 0
	add	r4, r4, r0
	.loc 3 81 0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L260
.LVL270:
.L265:
	.loc 3 104 0
	sub	r0, r4, r7
	.loc 3 105 0
	sub	sp, fp, #40
.LCFI137:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL271:
.L260:
.LCFI138:
	.loc 3 84 0
	ldrh	r3, [r6, r8]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L262
	.loc 3 85 0
	ldr	r2, [r6, #2260]
	lsl	r3, r3, #1
	mov	r0, r4
	ldr	r1, .L271+276
	ldrh	r2, [r2, r3]
	bl	sprintf
.LVL272:
	add	r4, r4, r0
.L262:
.LBB282:
	.loc 3 87 0
	mov	r0, #0
	.loc 3 91 0
	ldr	r9, .L271+280
	.loc 3 87 0
	bl	List_get_gc_head_node
.LVL273:
	uxth	r3, r0
.LVL274:
	.loc 3 88 0
	mov	r5, #0
	.loc 3 89 0
	movw	r10, #65535
.L264:
	cmp	r3, r10
	beq	.L263
	.loc 3 91 0 discriminator 2
	ldr	r2, [r6, #2248]
	lsl	r1, r3, #1
	mov	r8, #6
	mul	r8, r8, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp, #8]
	ldr	r2, [r6, #2252]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2260]
	ldrh	r2, [r2, r1]
	mov	r1, r9
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
.LVL275:
	.loc 3 88 0 discriminator 2
	add	r5, r5, #1
	.loc 3 92 0 discriminator 2
	ldr	r3, [r6, #2252]
	.loc 3 88 0 discriminator 2
	cmp	r5, #16
	.loc 3 91 0 discriminator 2
	add	r4, r4, r0
	.loc 3 92 0 discriminator 2
	ldrh	r3, [r3, r8]
.LVL276:
	.loc 3 88 0 discriminator 2
	bne	.L264
.L263:
.LBE282:
.LBB283:
	.loc 3 96 0
	ldr	r2, [r6, #2252]
	.loc 3 97 0
	mov	r8, #0
	.loc 3 96 0
	ldr	r3, [r6, #2272]
.LVL277:
	.loc 3 98 0
	movw	r9, #65535
	.loc 3 100 0
	ldr	r10, .L271+284
	.loc 3 96 0
	sub	r3, r3, r2
	ldr	r2, .L271+288
	asr	r3, r3, #1
	mul	r3, r2, r3
	uxth	r3, r3
.LVL278:
.L266:
	.loc 3 98 0
	cmp	r3, r9
	beq	.L265
	.loc 3 100 0 discriminator 2
	ldr	r1, [r6, #2248]
	lsl	r2, r3, #1
	mov	r5, #6
	mul	r5, r5, r3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, r10
	str	r2, [sp, #4]
	ldr	r2, [r6, #2252]
	add	r2, r2, r5
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r8
	.loc 3 97 0 discriminator 2
	add	r8, r8, #1
	.loc 3 100 0 discriminator 2
	bl	sprintf
.LVL279:
	.loc 3 97 0 discriminator 2
	cmp	r8, #4
	.loc 3 100 0 discriminator 2
	add	r4, r4, r0
	.loc 3 97 0 discriminator 2
	beq	.L265
	.loc 3 101 0
	ldr	r3, [r6, #2252]
	ldrh	r3, [r3, r5]
.LVL280:
	b	.L266
.L272:
	.align	2
.L271:
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
	.word	.LANCHOR0+2504
	.word	.LANCHOR0+2280
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
	.word	-1431655765
.LBE283:
.LFE140:
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
.LFB141:
	.loc 3 114 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL281:
	.movsp ip
	mov	ip, sp
.LCFI139:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI140:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI141:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 114 0
	mov	r5, r0
	.loc 3 120 0
	ldr	r2, .L274
	ldr	r1, .L274+4
	bl	sprintf
.LVL282:
	add	r4, r5, r0
	.loc 3 122 0
	mov	r0, r4
	bl	FtlPrintInfo2buf
.LVL283:
	add	r0, r4, r0
	.loc 3 124 0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L275:
	.align	2
.L274:
	.word	.LC70
	.word	.LC71
.LFE141:
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
.LFB236:
	.loc 2 2447 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI142:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI143:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI144:
	.pad #28
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2447 0
	.loc 2 2456 0
	ldr	r4, .L300
	ldr	r2, [r4, #2488]
	ldr	r3, [r4, #2500]
	cmp	r2, r3
	bcs	.L277
	ldr	r2, [r4, #2248]
	.loc 2 2458 0
	mov	r3, #0
	.loc 2 2459 0
	ldrh	r1, [r4, #16]
	.loc 2 2458 0
	str	r3, [r4, #2480]
	sub	r2, r2, #2
.L278:
	.loc 2 2459 0 discriminator 1
	cmp	r3, r1
	bcc	.L279
	.loc 2 2461 0
	ldr	r5, [r4, #2480]
	mov	r0, r5
	bl	__aeabi_uidiv
.LVL284:
	str	r0, [r4, #2488]
	.loc 2 2462 0
	ldr	r0, [r4, #2484]
	ldrh	r1, [r4, #68]
	sub	r0, r5, r0
	bl	__aeabi_uidiv
.LVL285:
	str	r0, [r4, #2480]
.L280:
	.loc 2 2468 0
	ldr	r6, [r4, #2500]
	ldr	r8, [r4, #2488]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L285
	.loc 2 2468 0 is_stmt 0 discriminator 1
	ldr	r2, [r4, #2496]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L285
.LVL286:
.L287:
	.loc 2 2469 0 is_stmt 1
	movw	r5, #65535
.L286:
	.loc 2 2518 0
	mov	r0, r5
	sub	sp, fp, #40
.LCFI145:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL287:
.L279:
.LCFI146:
	.loc 2 2460 0 discriminator 3
	ldrh	r0, [r2, #2]!
	.loc 2 2459 0 discriminator 3
	add	r3, r3, #1
	.loc 2 2460 0 discriminator 3
	ldr	ip, [r4, #2480]
	add	r0, r0, ip
	str	r0, [r4, #2480]
	b	.L278
.L277:
	.loc 2 2463 0
	ldr	r3, [r4, #2496]
	cmp	r2, r3
	.loc 2 2464 0
	addhi	r3, r3, #1
	strhi	r3, [r4, #2496]
	.loc 2 2465 0
	movhi	r3, #0
	.loc 2 2463 0
	bls	.L280
.L282:
	.loc 2 2465 0 discriminator 1
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	bcs	.L280
	.loc 2 2466 0 discriminator 3
	ldr	r0, [r4, #2248]
	lsl	r1, r3, #1
	.loc 2 2465 0 discriminator 3
	add	r3, r3, #1
	.loc 2 2466 0 discriminator 3
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L282
.L285:
	.loc 2 2471 0
	movw	r3, #2276
	ldrh	r0, [r4, r3]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
.LVL288:
	.loc 2 2472 0
	add	r3, r6, #64
	.loc 2 2471 0
	mov	r10, r0
.LVL289:
	.loc 2 2472 0
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r6, #30
	movls	r3, #0
	cmp	r3, #0
	bne	.L287
	.loc 2 2476 0
	ldr	r3, [r4, #2256]
.LVL290:
	cmp	r3, #0
	beq	.L287
	.loc 2 2483 0
	ldr	r0, [r4, #2252]
	.loc 2 2484 0
	movw	r7, #65535
.LVL291:
	ldr	r9, [r4, #2248]
	mov	r2, r7
	.loc 2 2483 0
	ldr	ip, .L300+4
	.loc 2 2495 0
	mov	lr, #6
.L288:
	.loc 2 2479 0
	ldrh	r1, [r3]
	movw	r5, #65535
	cmp	r1, r5
	bne	.L291
	mov	r5, r2
.L290:
	.loc 2 2498 0
	movw	r3, #65535
.LVL292:
	cmp	r5, r3
	beq	.L287
	.loc 2 2500 0
	lsl	r2, r5, #1
	ldrh	r3, [r9, r2]
.LVL293:
	.loc 2 2501 0
	cmp	r6, r3
	bcs	.L292
	str	r3, [fp, #-52]
	str	r2, [fp, #-48]
	.loc 2 2503 0
	bl	GetFreeBlockMinEraseCount
.LVL294:
	ldr	r2, [fp, #-48]
	cmp	r6, r0
	ldr	r3, [fp, #-52]
.LVL295:
	.loc 2 2504 0
	strcc	r7, [r4, #2500]
.L292:
	.loc 2 2506 0
	cmp	r8, r3
	bls	.L287
	.loc 2 2506 0 is_stmt 0 discriminator 1
	add	r1, r3, #128
	cmp	r10, r1
	ble	.L287
	.loc 2 2508 0 is_stmt 1
	add	r1, r3, #256
	cmp	r8, r1
	bhi	.L293
	.loc 2 2508 0 is_stmt 0 discriminator 1
	ldr	r1, [r4, #2496]
	add	r3, r3, #768
.LVL296:
	cmp	r3, r1
	bcs	.L287
.LVL297:
.L293:
	.loc 2 2510 0 is_stmt 1
	str	r10, [sp, #8]
	mov	r1, r5
	ldrh	r3, [r9, r2]
.LVL298:
	ldr	r0, .L300+8
	str	r3, [sp, #4]
	ldr	r3, [r4, #2260]
	ldrh	r3, [r3, r2]
	mov	r2, r8
	str	r3, [sp]
	ldr	r3, [r4, #2496]
	bl	printk
.LVL299:
	.loc 2 2511 0
	mov	r3, #1
	str	r3, [r4, #3140]
	.loc 2 2512 0
	b	.L286
.LVL300:
.L291:
	.loc 2 2481 0
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L289
	.loc 2 2483 0
	sub	r3, r3, r0
.LVL301:
	asr	r3, r3, #1
	mul	r3, ip, r3
	uxth	r5, r3
	.loc 2 2484 0
	lsl	r3, r5, #1
	ldrh	r3, [r9, r3]
	cmp	r6, r3
	bcs	.L290
	.loc 2 2483 0
	cmp	r7, r3
	movhi	r7, r3
	movhi	r2, r5
.LVL302:
.L289:
	.loc 2 2495 0
	mla	r3, lr, r1, r0
	b	.L288
.L301:
	.align	2
.L300:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC72
.LFE236:
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
.LFB237:
	.loc 2 2522 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL303:
	.movsp ip
	mov	ip, sp
.LCFI147:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI148:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI149:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2522 0
	.loc 2 2523 0
	movw	r2, #65535
	cmp	r0, r2
	beq	.L303
.LBB286:
.LBB287:
	.loc 2 2525 0
	ldr	r2, .L304
	lsl	r3, r0, #1
	mov	r1, #0
	ldr	r2, [r2, #2260]
	strh	r1, [r2, r3]	@ movhi
	.loc 2 2526 0
	bl	INSERT_FREE_LIST
.LVL304:
.L303:
.LBE287:
.LBE286:
	.loc 2 2528 0
	mov	r0, #0
.LVL305:
	ldmfd	sp, {fp, sp, pc}
.L305:
	.align	2
.L304:
	.word	.LANCHOR0
.LFE237:
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
.LFB240:
	.loc 2 2655 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL306:
	.movsp ip
	mov	ip, sp
.LCFI150:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI151:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI152:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2655 0
	.loc 2 2659 0
	movw	r3, #65535
	.loc 2 2655 0
	mov	r4, r0
	.loc 2 2659 0
	ldrh	r2, [r0]
	cmp	r2, r3
	bne	.L307
	.loc 2 2659 0 is_stmt 0 discriminator 1
	movw	r2, #2659
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	printk
.LVL307:
.L307:
	.loc 2 2660 0 is_stmt 1
	ldr	r5, .L320+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #78]
	cmp	r2, r3
	bne	.L308
	.loc 2 2660 0 is_stmt 0 discriminator 1
	movw	r2, #2660
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	printk
.LVL308:
.L308:
	.loc 2 2661 0 is_stmt 1
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L309
	.loc 2 2661 0 is_stmt 0 discriminator 1
	movw	r2, #2661
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	printk
.LVL309:
.L309:
	.loc 2 2663 0 is_stmt 1
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	.loc 2 2662 0
	mov	r3, #0
	.loc 2 2666 0
	ldrh	r1, [r5, #8]
	.loc 2 2664 0
	movw	ip, #65535
	.loc 2 2662 0
	strb	r3, [r4, #10]
	.loc 2 2663 0
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
.LVL310:
	.loc 2 2668 0
	mov	r2, r3
.L310:
	.loc 2 2664 0
	cmp	r0, ip
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L312
	.loc 2 2673 0
	ldrh	r2, [r4, #4]
	.loc 2 2672 0
	ldrh	r6, [r4, #2]
	.loc 2 2673 0
	sub	r2, r2, #1
	uxth	r2, r2
	.loc 2 2672 0
	orr	r6, r6, r0, lsl #10
	.loc 2 2680 0
	movw	r0, #65535
.LVL311:
	.loc 2 2673 0
	strh	r2, [r4, #4]	@ movhi
.L314:
	.loc 2 2675 0
	add	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2676 0
	cmp	r1, r3
	.loc 2 2677 0
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2678 0
	moveq	r3, #0
	.loc 2 2680 0
	add	ip, r4, r3, lsl #1
	ldrh	ip, [ip, #16]
	cmp	ip, r0
	beq	.L314
	strb	r3, [r4, #6]
	.loc 2 2682 0
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	ldrh	r1, [r5, #78]
	sub	r3, r3, r1
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L306
	.loc 2 2682 0 is_stmt 0 discriminator 2
	movw	r2, #2682
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	printk
.LVL312:
.L306:
	.loc 2 2684 0 is_stmt 1
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL313:
.L312:
	.loc 2 2665 0
	add	r3, r3, #1
	uxtb	r3, r3
	.loc 2 2666 0
	cmp	r3, r1
	.loc 2 2665 0
	strb	r3, [r4, #6]
	.loc 2 2667 0
	ldrheq	r3, [r4, #2]
	.loc 2 2668 0
	strbeq	r2, [r4, #6]
	.loc 2 2667 0
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	.loc 2 2670 0
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #16]
	b	.L310
.L321:
	.align	2
.L320:
	.word	.LANCHOR1+137
	.word	.LC1
	.word	.LANCHOR0
.LFE240:
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
.LFB243:
	.file 5 "drivers/rkflash/rksftl/sftl_gc.c"
	.loc 5 22 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI153:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI154:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI155:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 22 0
	.loc 5 27 0
	mov	lr, #12
	mov	r4, #1
	.loc 5 25 0
	ldr	r3, .L327
	.loc 5 30 0
	mov	r5, #20
	.loc 5 25 0
	mov	r2, #0
	str	r2, [r3, #3144]
.L323:
	.loc 5 26 0 discriminator 1
	ldrh	r1, [r3, #8]
	uxth	r0, r2
	add	ip, r2, #1
	cmp	r0, r1
	bcc	.L324
	.loc 5 35 0 discriminator 1
	mov	ip, #12
	mov	lr, #0
.L325:
	.loc 5 34 0 discriminator 1
	ldr	r2, [r3, #3164]
	cmp	r1, r2
	bcc	.L326
	.loc 5 39 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L324:
	uxth	r2, r2
	.loc 5 27 0 discriminator 3
	ldr	r6, [r3, #3148]
	mul	r0, lr, r2
	add	r1, r6, r0
	str	r4, [r1, #8]
	.loc 5 28 0 discriminator 3
	ldrh	r1, [r3, #86]
	mul	r1, r2, r1
	add	r7, r1, #3
	cmp	r1, #0
	movlt	r1, r7
	ldr	r7, [r3, #3152]
	bic	r1, r1, #3
	add	r1, r7, r1
	str	r1, [r6, r0]
	.loc 5 29 0 discriminator 3
	ldrh	r1, [r3, #88]
	ldr	r7, [r3, #3148]
	mul	r1, r2, r1
	add	r6, r7, r0
	add	r8, r1, #3
	cmp	r1, #0
	movlt	r1, r8
	ldr	r8, [r3, #3156]
	bic	r1, r1, #3
	add	r1, r8, r1
	str	r1, [r6, #4]
	.loc 5 30 0 discriminator 3
	ldr	r1, [r3, #3160]
	mla	r2, r5, r2, r1
	ldr	r1, [r7, r0]
	str	r1, [r2, #8]
	.loc 5 31 0 discriminator 3
	ldr	r1, [r6, #4]
	str	r1, [r2, #12]
	mov	r2, ip
	b	.L323
.L326:
	.loc 5 35 0 discriminator 3
	mul	r4, ip, r1
	ldr	r5, [r3, #3148]
	add	r2, r5, r4
	str	lr, [r2, #8]
	.loc 5 36 0 discriminator 3
	ldrh	r2, [r3, #86]
	mul	r2, r1, r2
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	ldr	r0, [r3, #3152]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r5, r4]
	.loc 5 37 0 discriminator 3
	ldrh	r2, [r3, #88]
	ldr	r0, [r3, #3148]
	mul	r2, r1, r2
	add	r0, r0, r4
	.loc 5 34 0 discriminator 3
	add	r1, r1, #1
	uxth	r1, r1
	.loc 5 37 0 discriminator 3
	add	r4, r2, #3
	cmp	r2, #0
	movlt	r2, r4
	ldr	r4, [r3, #3156]
	bic	r2, r2, #3
	add	r2, r4, r2
	str	r2, [r0, #4]
	b	.L325
.L328:
	.align	2
.L327:
	.word	.LANCHOR0
.LFE243:
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
.LFB244:
	.loc 5 42 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL314:
	.movsp ip
	mov	ip, sp
.LCFI156:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI157:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI158:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 42 0
	.loc 5 46 0
	mov	lr, #0
	.loc 5 48 0
	mov	r5, #20
	.loc 5 47 0
	ldr	r3, .L336
	.loc 5 48 0
	mov	r7, #12
	.loc 5 49 0
	mov	r8, lr
	.loc 5 47 0
	ldr	r6, [r3, #3164]
	.loc 5 48 0
	ldr	r4, [r3, #3148]
.L330:
	uxth	r3, lr
	.loc 5 46 0 discriminator 1
	cmp	r1, r3
	ldmfdls	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	.loc 5 48 0
	mla	ip, r5, r3, r0
	mov	r2, #0
.L331:
	uxth	r3, r2
	.loc 5 47 0 discriminator 1
	cmp	r6, r3
	bls	.L332
	.loc 5 48 0
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L331
	.loc 5 49 0
	str	r8, [r9, #8]
.L332:
	add	lr, lr, #1
	b	.L330
.L337:
	.align	2
.L336:
	.word	.LANCHOR0
.LFE244:
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
.LFB245:
	.loc 5 57 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL315:
	.movsp ip
	mov	ip, sp
.LCFI159:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI160:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI161:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 57 0
	.loc 5 61 0
	mov	ip, #0
	.loc 5 63 0
	mov	r6, #12
	.loc 5 62 0
	ldr	r3, .L345
	.loc 5 64 0
	mov	r7, #1
	.loc 5 65 0
	mov	r8, #20
	.loc 5 62 0
	ldr	r4, [r3, #3164]
	.loc 5 63 0
	ldr	r5, [r3, #3148]
.L339:
	uxth	r2, ip
	.loc 5 61 0 discriminator 1
	cmp	r1, r2
	bhi	.L343
	.loc 5 71 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L343:
	mov	lr, #0
.L340:
	uxth	r3, lr
	.loc 5 62 0 discriminator 1
	cmp	r4, r3
	bls	.L341
	.loc 5 63 0
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L340
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
.L341:
	add	ip, ip, #1
	b	.L339
.L346:
	.align	2
.L345:
	.word	.LANCHOR0
.LFE245:
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
.LFB246:
	.loc 5 74 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL316:
	.movsp ip
	mov	ip, sp
.LCFI162:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI163:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI164:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 74 0
	.loc 5 77 0
	movw	r1, #3172
	.loc 5 78 0
	ldr	r2, .L352
	ldr	r3, [r2, #3168]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L348:
	.loc 5 77 0 discriminator 1
	cmp	r3, r2
	bne	.L350
	.loc 5 81 0
	mov	r0, #0
.LVL317:
	ldmfd	sp, {fp, sp, pc}
.LVL318:
.L350:
	.loc 5 78 0
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L348
	.loc 5 79 0
	mov	r0, #1
.LVL319:
	.loc 5 82 0
	ldmfd	sp, {fp, sp, pc}
.L353:
	.align	2
.L352:
	.word	.LANCHOR0
.LFE246:
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
.LFB247:
	.loc 5 85 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL320:
	.movsp ip
	mov	ip, sp
.LCFI165:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI166:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI167:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 85 0
	mov	r4, r0
	.loc 5 88 0
	ubfx	r0, r0, #10, #16
.LVL321:
	.loc 5 85 0
	mov	r5, r1
	mov	r6, r2
	.loc 5 88 0
	bl	P2V_block_in_plane
.LVL322:
	.loc 5 91 0
	ldr	r3, .L358
	movw	r2, #3172
	mov	ip, #0
	ldrh	lr, [r3, r2]
	.loc 5 92 0
	ldr	r2, [r3, #3168]
	sub	r1, r2, #2
.LVL323:
.L355:
	uxth	r7, ip
.LVL324:
	.loc 5 91 0 discriminator 1
	cmp	r7, lr
	bcc	.L357
	.loc 5 96 0
	bne	.L356
	.loc 5 97 0
	lsl	ip, r7, #1
.LVL325:
	.loc 5 98 0
	movw	r1, #3172
	.loc 5 97 0
	strh	r0, [r2, ip]	@ movhi
	.loc 5 98 0
	ldrh	r2, [r3, r1]
	add	r2, r2, #1
	strh	r2, [r3, r1]	@ movhi
	b	.L356
.LVL326:
.L357:
	.loc 5 92 0
	ldrh	r7, [r1, #2]!
.LVL327:
	add	ip, ip, #1
	cmp	r7, r0
	bne	.L355
.LVL328:
.L356:
	.loc 5 101 0
	movw	ip, #3180
	mov	r0, #12
.LVL329:
	ldrh	r2, [r3, ip]
	mul	r0, r0, r2
	ldr	r2, [r3, #3176]
	add	r1, r2, r0
	.loc 5 102 0
	stmib	r1, {r5, r6}
.LVL330:
	.loc 5 103 0
	str	r4, [r2, r0]
	.loc 5 107 0
	ldrh	r2, [r3, ip]
	add	r2, r2, #1
	strh	r2, [r3, ip]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L359:
	.align	2
.L358:
	.word	.LANCHOR0
.LFE247:
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
.LFB254:
	.loc 5 345 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL331:
	.movsp ip
	mov	ip, sp
.LCFI168:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI169:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI170:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 345 0
	mov	r4, r0
	.loc 5 347 0
	mov	r1, r0
	ldr	r0, .L363
.LVL332:
	bl	printk
.LVL333:
	.loc 5 349 0
	ldr	r3, .L363+4
	movw	ip, #3182
	ldrh	lr, [r3, ip]
	cmp	r4, lr
	beq	.L361
	.loc 5 349 0 is_stmt 0 discriminator 1
	add	r2, r3, #3184
	ldrh	r0, [r2]
	cmp	r4, r0
	beq	.L361
	.loc 5 351 0 is_stmt 1
	movw	r1, #65535
	cmp	lr, r1
	.loc 5 352 0
	strheq	r4, [r3, ip]	@ movhi
	.loc 5 351 0
	beq	.L361
	.loc 5 353 0
	cmp	r0, r1
	.loc 5 354 0
	strheq	r4, [r2]	@ movhi
.L361:
	.loc 5 356 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L364:
	.align	2
.L363:
	.word	.LC73
	.word	.LANCHOR0
.LFE254:
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
.LFB255:
	.loc 5 359 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL334:
	.movsp ip
	mov	ip, sp
.LCFI171:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI172:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI173:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 359 0
	.loc 5 364 0
	movw	r6, #3186
	.loc 5 359 0
	mov	r5, r0
	.loc 5 364 0
	ldr	r4, .L369
	.loc 5 361 0
	bl	P2V_block_in_plane
.LVL335:
	.loc 5 364 0
	mov	r2, r5
	.loc 5 361 0
	mov	r7, r0
	.loc 5 364 0
	ldrh	r1, [r4, r6]
	ldr	r0, .L369+4
	bl	printk
.LVL336:
	.loc 5 366 0
	mov	r0, r7
	bl	FtlGcRefreshBlock
.LVL337:
	.loc 5 368 0
	ldrh	r3, [r4, r6]
	mov	r2, #0
	.loc 5 369 0
	ldr	r0, .L369+8
.L366:
	.loc 5 368 0 discriminator 1
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L368
	.loc 5 372 0
	cmp	r3, #15
	.loc 5 373 0
	movwls	r2, #3186
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L369+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L367
.L368:
	add	r2, r2, #1
	.loc 5 369 0
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L366
.L367:
	.loc 5 375 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L370:
	.align	2
.L369:
	.word	.LANCHOR0
	.word	.LC74
	.word	.LANCHOR0+3188
.LFE255:
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
.LFB256:
	.loc 5 379 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI174:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI175:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI176:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 379 0
	.loc 5 380 0
	movw	r3, #3186
	ldr	r4, .L377
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L372
	.loc 5 380 0 is_stmt 0 discriminator 1
	movw	r3, #3182
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L372
	.loc 5 381 0 is_stmt 1
	movw	r3, #3222
.LBB290:
.LBB291:
	.loc 5 383 0
	movw	r5, #3222
.LBE291:
.LBE290:
	.loc 5 381 0
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	.loc 5 382 0
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
.LBB293:
.LBB292:
	.loc 5 383 0
	ldrh	r3, [r4, r5]
	ldr	r2, .L377+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
.LVL338:
	bl	FtlGcRefreshBlock
.LVL339:
	.loc 5 384 0
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L372:
.LBE292:
.LBE293:
	.loc 5 387 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L378:
	.align	2
.L377:
	.word	.LANCHOR0
	.word	.LANCHOR0+3188
.LFE256:
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
.LFB262:
	.loc 1 44 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL340:
	.movsp ip
	mov	ip, sp
.LCFI177:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI178:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI179:
	.pad #20
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 44 0
	.loc 1 46 0
	ldr	r3, .L397
	.loc 1 44 0
	mov	r7, r1
	.loc 1 51 0
	ldr	r8, .L397+4
	mov	r4, r0
	ldr	r9, .L397+8
	.loc 1 50 0
	mov	r5, #0
.LVL341:
	.loc 1 46 0
	ldrh	r10, [r3, #12]
.LVL342:
	str	r3, [fp, #-56]
.LVL343:
.L380:
	.loc 1 50 0 discriminator 1
	cmp	r5, r7
	bne	.L387
	.loc 1 71 0
	mov	r0, #0
.LVL344:
	sub	sp, fp, #40
.LCFI180:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL345:
.L387:
.LCFI181:
	.loc 1 51 0
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L381
	.loc 1 51 0 is_stmt 0 discriminator 2
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L382
.L381:
	.loc 1 51 0 discriminator 3
	mov	r2, #51
.LVL346:
	mov	r1, r8
	mov	r0, r9
.LVL347:
	bl	printk
.LVL348:
.L382:
	.loc 1 53 0 is_stmt 1
	ldr	r6, .L397+12
	.loc 1 52 0
	sub	r2, fp, #52
.LVL349:
	sub	r1, fp, #48
	mov	r0, r4
.LVL350:
	bl	l2p_addr_tran
.LVL351:
	.loc 1 53 0
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-48]
	ldrb	r0, [fp, #-52]	@ zero_extendqisi2
	ldr	ip, [r6, #12]
	blx	ip
.LVL352:
	.loc 1 58 0
	ldr	r3, [fp, #-56]
	.loc 1 57 0
	str	r0, [r4]
	.loc 1 58 0
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L384
	.loc 1 59 0
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-48]
	ldr	r6, [r6, #12]
	add	r3, r3, #8
	add	r2, r2, #2048
	ldrb	r0, [fp, #-52]	@ zero_extendqisi2
	add	r1, r10, r1
	blx	r6
.LVL353:
	.loc 1 63 0
	cmn	r0, #1
	beq	.L385
	.loc 1 64 0 discriminator 1
	ldr	r3, [r4, #12]
	.loc 1 63 0 discriminator 1
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L384
	.loc 1 64 0
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L384
	.loc 1 65 0
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L384
.L385:
	.loc 1 67 0
	mvn	r3, #0
	str	r3, [r4]
.L384:
	.loc 1 50 0 discriminator 2
	add	r5, r5, #1
	add	r4, r4, #20
	b	.L380
.L398:
	.align	2
.L397:
	.word	g_nand_phy_info
	.word	.LANCHOR1+156
	.word	.LC1
	.word	g_nand_ops
.LFE262:
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
.LFB170:
	.loc 4 279 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI182:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI183:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI184:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 279 0
	.loc 4 289 0
	mov	r6, #0
.LVL354:
	.loc 4 290 0
	mvn	r9, #0
	.loc 4 286 0
	ldr	r4, .L409
	.loc 4 294 0
	ldr	r10, .L409+4
	.loc 4 286 0
	ldr	r3, [r4, #2244]
	add	r7, r4, #138
	.loc 4 287 0
	ldr	r8, [r4, #3244]
	.loc 4 286 0
	str	r3, [r4, #3232]
	.loc 4 287 0
	str	r8, [r4, #3236]
.LVL355:
.L400:
	.loc 4 289 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r6, r3
	bcc	.L405
	.loc 4 314 0
	mov	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L405:
	.loc 4 291 0
	ldrh	r5, [r4, #74]
.LVL356:
	.loc 4 290 0
	strh	r9, [r7, #2]!	@ movhi
.LVL357:
.L402:
	.loc 4 291 0 discriminator 2
	ldrh	r3, [r4, #74]
	sub	r5, r5, #1
.LVL358:
	uxth	r5, r5
.LVL359:
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L403
	.loc 4 292 0
	mla	r3, r6, r3, r5
	.loc 4 294 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	.loc 4 292 0
	lsl	r3, r3, #10
.LVL360:
	.loc 4 293 0
	str	r3, [r4, #3228]
	.loc 4 294 0
	bl	FlashReadPages
.LVL361:
	.loc 4 295 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	beq	.L402
	.loc 4 301 0
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L402
	.loc 4 303 0
	strh	r5, [r7]	@ movhi
.L403:
	.loc 4 289 0 discriminator 2
	add	r6, r6, #1
	b	.L400
.L410:
	.align	2
.L409:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
.LFE170:
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
.LFB176:
	.loc 2 47 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL362:
	.movsp ip
	mov	ip, sp
.LCFI185:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, fp, ip, lr}
.LCFI186:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI187:
	.pad #92
	sub	sp, sp, #92
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 47 0
	ldr	r3, .L419
	.loc 2 51 0
	cmp	r1, #1
	.loc 2 57 0
	lsl	r7, r0, #10
	.loc 2 58 0
	mov	r2, r1
	.loc 2 47 0
	mov	r6, r1
	.loc 2 51 0
	ldrheq	r5, [r3, #80]
	.loc 2 58 0
	mov	r1, #1
.LVL363:
	.loc 2 51 0
	ldrhne	r5, [r3, #78]
	.loc 2 58 0
	sub	r0, fp, #120
.LVL364:
	.loc 2 53 0
	ldr	r3, .L419+4
	.loc 2 56 0
	sub	r5, r5, #1
	sxth	r5, r5
.LVL365:
	.loc 2 53 0
	str	r3, [fp, #-112]
	.loc 2 54 0
	sub	r3, fp, #100
	str	r3, [fp, #-108]
	.loc 2 57 0
	orr	r3, r5, r7
	str	r3, [fp, #-116]
	.loc 2 58 0
	bl	FlashReadPages
.LVL366:
	.loc 2 59 0
	ldr	r3, [fp, #-100]
	cmn	r3, #1
	moveq	r8, #0
.LVL367:
	beq	.L415
.L414:
	.loc 2 73 0
	mov	r0, r5
	sub	sp, fp, #32
.LCFI188:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL368:
.L418:
.LCFI189:
	.loc 2 61 0
	add	r3, r8, r5
	.loc 2 63 0
	mov	r2, r6
	.loc 2 61 0
	add	r3, r3, r3, lsr #31
	.loc 2 63 0
	mov	r1, #1
	sub	r0, fp, #120
	.loc 2 61 0
	asr	r4, r3, #1
	.loc 2 62 0
	sxth	r3, r4
	orr	r3, r3, r7
	str	r3, [fp, #-116]
	.loc 2 63 0
	bl	FlashReadPages
.LVL369:
	.loc 2 65 0
	ldr	r3, [fp, #-100]
	cmn	r3, #1
	bne	.L416
	.loc 2 65 0 is_stmt 0 discriminator 1
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	.loc 2 66 0 is_stmt 1 discriminator 1
	subeq	r4, r4, #1
	sxtheq	r5, r4
	.loc 2 65 0 discriminator 1
	beq	.L415
.L416:
	.loc 2 68 0
	add	r4, r4, #1
	sxth	r8, r4
.L415:
	.loc 2 60 0
	cmp	r8, r5
	ble	.L418
	b	.L414
.L420:
	.align	2
.L419:
	.word	.LANCHOR0
	.word	ftl_temp_buf
.LFE176:
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
.LFB263:
	.loc 1 74 0
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL370:
	.movsp ip
	mov	ip, sp
.LCFI190:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI191:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI192:
	.pad #44
	sub	sp, sp, #44
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 74 0
	mov	r8, r3
	.loc 1 76 0
	ldr	r3, .L451
.LVL371:
	.loc 1 74 0
	mov	r7, r2
	mov	r9, r1
	mov	r4, r0
	.loc 1 80 0
	mov	r5, r0
.LVL372:
	.loc 1 76 0
	ldrh	r2, [r3, #12]
.LVL373:
	.loc 1 80 0
	mov	r6, #0
	str	r3, [fp, #-84]
	.loc 1 76 0
	str	r2, [fp, #-80]
.LVL374:
.L422:
	.loc 1 80 0 discriminator 1
	cmp	r6, r9
	bne	.L430
	.loc 1 101 0
	cmp	r8, #0
	bne	.L437
.L450:
	.loc 1 131 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI193:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL375:
.L430:
.LCFI194:
	.loc 1 81 0
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L423
	.loc 1 81 0 is_stmt 0 discriminator 2
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L424
.L423:
	.loc 1 81 0 discriminator 3
	mov	r2, #81
.LVL376:
	ldr	r1, .L451+4
	ldr	r0, .L451+8
	bl	printk
.LVL377:
.L424:
	.loc 1 82 0 is_stmt 1
	sub	r2, fp, #72
.LVL378:
	sub	r1, fp, #68
	mov	r0, r5
	bl	l2p_addr_tran
.LVL379:
	.loc 1 83 0
	ldr	r3, .L451+12
	ldr	r2, [r5, #8]
	ldr	r1, [fp, #-68]
	ldr	r10, [r3, #8]
	ldrb	r0, [fp, #-72]	@ zero_extendqisi2
	ldr	r3, [r5, #12]
	blx	r10
.LVL380:
	.loc 1 88 0
	cmp	r0, #0
	.loc 1 89 0
	mvnne	r3, #0
	.loc 1 87 0
	streq	r0, [r5]
	.loc 1 89 0
	strne	r3, [r5]
	.loc 1 91 0
	ldr	r3, [fp, #-84]
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	bne	.L428
	.loc 1 92 0
	ldr	r1, .L451+12
	ldr	r0, [fp, #-80]
.LVL381:
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r10, [r1, #8]
	ldr	r1, [fp, #-68]
	add	r3, r3, #8
	add	r2, r2, #2048
	add	r1, r0, r1
	ldrb	r0, [fp, #-72]	@ zero_extendqisi2
.LVL382:
	blx	r10
.LVL383:
	.loc 1 96 0
	cmp	r0, #0
	.loc 1 97 0
	mvnne	r3, #0
	strne	r3, [r5]
.L428:
	.loc 1 80 0 discriminator 2
	add	r6, r6, #1
	add	r5, r5, #20
	b	.L422
.LVL384:
.L435:
.LBB294:
	.loc 1 106 0
	ldr	r9, .L451+16
.LVL385:
	mov	r3, #0
	.loc 1 107 0
	str	r3, [r8, #3248]
	.loc 1 112 0
	mov	r2, r7
.LVL386:
	mov	r1, #1
.LVL387:
	sub	r0, fp, #64
	.loc 1 106 0
	str	r3, [r9]
	.loc 1 108 0
	ldr	r3, [r4, #4]
	.loc 1 109 0
	str	r9, [fp, #-56]
	.loc 1 110 0
	str	r10, [fp, #-52]
	.loc 1 108 0
	str	r3, [fp, #-60]
	.loc 1 112 0
	bl	FlashReadPages
.LVL388:
	.loc 1 113 0
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	bne	.L432
	.loc 1 114 0
	ldr	r1, [r4, #4]
	ldr	r0, .L451+20
	str	r3, [fp, #-80]
	bl	printk
.LVL389:
	.loc 1 115 0
	ldr	r3, [fp, #-80]
	str	r3, [r4]
.L432:
	.loc 1 117 0
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L433
	.loc 1 118 0
	ldr	r2, [r3]
	ldr	r3, [r8, #3248]
	cmp	r2, r3
	beq	.L433
	.loc 1 119 0
	ldr	r1, [r4, #4]
	ldr	r0, .L451+24
	bl	printk
.LVL390:
	.loc 1 120 0
	mvn	r3, #0
	str	r3, [r4]
.L433:
	.loc 1 123 0
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L434
	.loc 1 124 0
	ldr	r2, [r3]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L434
	.loc 1 125 0
	ldr	r1, [r4, #4]
	ldr	r0, .L451+28
	bl	printk
.LVL391:
	.loc 1 126 0
	mvn	r3, #0
	str	r3, [r4]
.L434:
.LBE294:
	.loc 1 103 0 discriminator 2
	add	r5, r5, #1
	add	r4, r4, #20
.LVL392:
.L431:
	.loc 1 103 0 is_stmt 0 discriminator 1
	cmp	r6, r5
	bne	.L435
	b	.L450
.LVL393:
.L437:
.LBB295:
	.loc 1 107 0 is_stmt 1
	ldr	r8, .L451+32
.LVL394:
.LBE295:
	mov	r5, #0
.LBB296:
	.loc 1 110 0
	add	r10, r8, #3248
	b	.L431
.L452:
	.align	2
.L451:
	.word	g_nand_phy_info
	.word	.LANCHOR1+171
	.word	.LC1
	.word	g_nand_ops
	.word	check_buf
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR0
.LBE296:
.LFE263:
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
.LFB264:
	.loc 1 134 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL395:
	.movsp ip
	mov	ip, sp
.LCFI195:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI196:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI197:
	.pad #8
	sub	sp, sp, #8
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 134 0
	.loc 1 137 0
	ldr	r3, .L464
	.loc 1 134 0
	mov	r8, r2
	.loc 1 142 0
	ldr	r7, .L464+4
	mov	r4, r0
	.loc 1 140 0
	mov	r6, #0
.LVL396:
	.loc 1 137 0
	ldrh	r9, [r3, #12]
.LVL397:
	mov	r5, r3
.LVL398:
.L454:
	.loc 1 140 0 discriminator 1
	cmp	r6, r8
	bne	.L460
	.loc 1 153 0
	mov	r0, #0
.LVL399:
	sub	sp, fp, #36
.LCFI198:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL400:
.L460:
.LCFI199:
	.loc 1 141 0
	sub	r2, fp, #44
	sub	r1, fp, #40
.LVL401:
	mov	r0, r4
.LVL402:
	bl	l2p_addr_tran
.LVL403:
	.loc 1 142 0
	ldr	r3, [r7, #4]
	ldr	r1, [fp, #-40]
	ldrb	r0, [fp, #-44]	@ zero_extendqisi2
	blx	r3
.LVL404:
	.loc 1 144 0
	cmp	r0, #0
	.loc 1 145 0
	mvnne	r3, #0
	.loc 1 143 0
	streq	r0, [r4]
	.loc 1 145 0
	strne	r3, [r4]
	.loc 1 146 0
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L458
	.loc 1 147 0
	ldr	r1, [fp, #-40]
	ldr	r3, [r7, #4]
	ldrb	r0, [fp, #-44]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r3
.LVL405:
	.loc 1 148 0
	cmp	r0, #0
	.loc 1 149 0
	mvnne	r3, #0
	strne	r3, [r4]
.L458:
	.loc 1 140 0 discriminator 2
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L454
.L465:
	.align	2
.L464:
	.word	g_nand_phy_info
	.word	g_nand_ops
.LFE264:
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
.LFB180:
	.loc 2 98 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL406:
	.movsp ip
	mov	ip, sp
.LCFI200:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI201:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI202:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 98 0
	mov	r6, r0
.LBB297:
.LBB298:
	.loc 2 94 0
	ldr	r4, .L475
.LBE298:
.LBE297:
	.loc 2 99 0
	ldrh	r3, [r4, #194]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
.LBB299:
	.loc 2 101 0
	cmp	r1, #0
	beq	.L468
.LBB300:
	.loc 2 102 0
	bl	P2V_block_in_plane
.LVL407:
	mov	r5, r0
	.loc 2 103 0
	ldr	r0, [r4, #3760]
	lsl	r3, r6, #10
	.loc 2 104 0
	mov	r2, #1
	mov	r1, r2
	.loc 2 103 0
	str	r3, [r0, #4]
	.loc 2 104 0
	bl	FlashEraseBlocks
.LVL408:
	.loc 2 105 0
	ldr	r2, [r4, #2248]
	lsl	r0, r5, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	.loc 2 106 0
	ldr	r3, [r4, #2492]
	add	r3, r3, #1
	str	r3, [r4, #2492]
.LVL409:
.L468:
.LBE300:
	.loc 2 108 0
	ldrh	r3, [r4, #194]
	add	r3, r3, #1
	strh	r3, [r4, #194]	@ movhi
	.loc 2 109 0
	ldrh	r3, [r4, #192]
	add	r2, r4, r3, lsl #1
	.loc 2 110 0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	.loc 2 109 0
	strh	r6, [r2, #196]	@ movhi
	.loc 2 110 0
	strh	r3, [r4, #192]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L476:
	.align	2
.L475:
	.word	.LANCHOR0
.LBE299:
.LFE180:
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
.LFB149:
	.loc 3 528 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL410:
	.movsp ip
	mov	ip, sp
.LCFI203:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI204:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI205:
	.pad #28
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 528 0
	.loc 3 538 0
	ldr	r3, .L519
	.loc 3 539 0
	mov	r10, #0
	.loc 3 528 0
	mov	r6, r1
	.loc 3 536 0
	mov	r5, r10
	.loc 3 537 0
	mov	r4, r10
.LVL411:
	.loc 3 540 0
	mov	r9, #20
	mov	r8, r3
	.loc 3 528 0
	str	r0, [fp, #-52]
	.loc 3 538 0
	str	r0, [r3, #3764]
.L478:
	.loc 3 539 0 discriminator 1
	ldrh	r0, [r8, #8]
.LVL412:
	uxth	r1, r10
	cmp	r0, r1
	bhi	.L482
	.loc 3 559 0
	cmp	r5, #0
	beq	.L477
	.loc 3 563 0
	mov	r7, #0
	.loc 3 564 0
	mov	r9, #20
	.loc 3 562 0
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r8, #3760]
	bl	FlashEraseBlocks
.LVL413:
.L485:
	.loc 3 563 0 discriminator 1
	uxth	r2, r7
	cmp	r5, r2
	bhi	.L487
.L488:
	.loc 3 571 0
	cmp	r6, #0
	.loc 3 578 0
	ldr	r7, .L519
	.loc 3 573 0
	ldrhne	r3, [r8, #80]
.LVL414:
	.loc 3 533 0
	moveq	r3, #6
.LVL415:
	streq	r3, [fp, #-64]
	.loc 3 531 0
	moveq	r3, #1
.LVL416:
	mov	r10, r7
	streq	r3, [fp, #-56]
	.loc 3 573 0
	strne	r3, [fp, #-56]
	.loc 3 574 0
	lsrne	r3, r3, #2
.LVL417:
	.loc 3 534 0
	streq	r6, [fp, #-60]
	.loc 3 574 0
	strne	r3, [fp, #-64]
	.loc 3 572 0
	movne	r3, #1
.LVL418:
	strne	r3, [fp, #-60]
	mov	r3, #0
.LVL419:
	str	r3, [fp, #-48]
.LVL420:
.L497:
	.loc 3 578 0
	mov	r8, #0
	.loc 3 577 0
	mov	r5, r8
.L489:
	.loc 3 578 0 discriminator 1
	ldrh	r1, [r7, #8]
	uxth	r3, r8
.LVL421:
	cmp	r1, r3
	bhi	.L492
	.loc 3 593 0
	cmp	r5, #0
	beq	.L477
	.loc 3 596 0
	ldr	r2, [fp, #-60]
.LVL422:
	mov	r3, #1
	mov	r1, r5
	ldr	r0, [r7, #3760]
	bl	FlashProgPages
.LVL423:
	.loc 3 597 0
	mov	r9, #0
	.loc 3 598 0
	mov	r2, #20
.L494:
	.loc 3 597 0 discriminator 1
	uxth	r3, r9
	cmp	r5, r3
	bhi	.L496
	.loc 3 604 0
	ldr	r3, [fp, #-48]
.LVL424:
	ldr	r2, [fp, #-64]
.LVL425:
	add	r3, r3, r2
.LVL426:
	uxth	r3, r3
.LVL427:
	str	r3, [fp, #-48]
	.loc 3 605 0
	ldr	r2, [fp, #-48]
.LVL428:
	ldr	r3, [fp, #-56]
.LVL429:
	cmp	r3, r2
	bhi	.L497
	mov	r7, #0
	.loc 3 611 0 discriminator 1
	mov	r9, #20
.L498:
	.loc 3 610 0 discriminator 1
	uxth	r3, r7
.LVL430:
	cmp	r5, r3
	bhi	.L500
	.loc 3 615 0
	ldr	r3, [fp, #-52]
.LVL431:
	adds	r6, r6, #0
.LVL432:
	movne	r6, #1
	cmp	r3, #63
	movhi	r8, r6
	orrls	r8, r6, #1
	cmp	r8, #0
	beq	.L477
	.loc 3 616 0
	mov	r2, r5
.LVL433:
	ldr	r1, [fp, #-60]
	ldr	r0, [r10, #3760]
	bl	FlashEraseBlocks
.LVL434:
.L477:
	.loc 3 619 0
	mov	r0, r4
	sub	sp, fp, #40
.LCFI206:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL435:
.L482:
.LCFI207:
	.loc 3 540 0
	uxth	r1, r10
	ldr	ip, [r8, #3760]
	mov	r3, #0
	mul	r0, r9, r1
	str	r3, [ip, r0]
	.loc 3 541 0
	add	r0, r8, r1
	ldrb	r0, [r0, #36]	@ zero_extendqisi2
	ldr	r1, [fp, #-52]
	bl	V2P_block
.LVL436:
	.loc 3 543 0
	cmp	r6, #0
	.loc 3 541 0
	mov	r7, r0
	.loc 3 543 0
	beq	.L479
	.loc 3 544 0
	bl	IsBlkInVendorPart
.LVL437:
	cmp	r0, #0
	bne	.L480
.L479:
	.loc 3 548 0
	mov	r0, r7
	bl	FtlBbmIsBadBlock
.LVL438:
	cmp	r0, #0
	.loc 3 554 0
	addne	r4, r4, #1
.LVL439:
	uxthne	r4, r4
.LVL440:
	.loc 3 548 0
	bne	.L480
	.loc 3 549 0
	ldr	r0, [r8, #3760]
	lsl	r7, r7, #10
	.loc 3 550 0
	ldr	r1, [r8, #3768]
	.loc 3 549 0
	mla	r0, r9, r5, r0
	.loc 3 550 0
	str	r1, [r0, #8]
	.loc 3 551 0
	ldrh	r1, [r8, #88]
	.loc 3 549 0
	str	r7, [r0, #4]
	.loc 3 551 0
	mul	r1, r5, r1
	.loc 3 552 0
	add	r5, r5, #1
	uxth	r5, r5
	.loc 3 551 0
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	ldr	ip, [r8, #3772]
	bic	r1, r1, #3
	add	r1, ip, r1
	str	r1, [r0, #12]
.L480:
	add	r10, r10, #1
	b	.L478
.L487:
	.loc 3 564 0
	mul	r2, r9, r7
	ldr	r1, [r8, #3760]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L486
	.loc 3 566 0
	ldr	r0, [r0, #4]
	.loc 3 565 0
	add	r4, r4, #1
.LVL441:
	uxth	r4, r4
.LVL442:
	.loc 3 566 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL443:
.L486:
	add	r7, r7, #1
	b	.L485
.LVL444:
.L492:
	.loc 3 579 0
	uxth	r3, r8
	mov	r2, #20
.LVL445:
	ldr	r0, [r7, #3760]
	mul	r1, r2, r3
	mov	r2, #0
	.loc 3 580 0
	add	r3, r7, r3
	.loc 3 579 0
	str	r2, [r0, r1]
	.loc 3 580 0
	ldr	r1, [fp, #-52]
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
	bl	V2P_block
.LVL446:
	.loc 3 581 0
	cmp	r6, #0
	.loc 3 580 0
	mov	r9, r0
	.loc 3 581 0
	beq	.L490
	.loc 3 582 0
	bl	IsBlkInVendorPart
.LVL447:
	cmp	r0, #0
	bne	.L491
.L490:
	.loc 3 585 0
	mov	r0, r9
	bl	FtlBbmIsBadBlock
.LVL448:
	cmp	r0, #0
	bne	.L491
	.loc 3 586 0
	ldr	r1, [r7, #3760]
	mov	r3, #20
	mla	r1, r3, r5, r1
	ldr	r3, [fp, #-48]
.LVL449:
	add	r9, r3, r9, lsl #10
	.loc 3 587 0
	ldr	r3, [r7, #3776]
.LVL450:
	.loc 3 586 0
	str	r9, [r1, #4]
	.loc 3 587 0
	str	r3, [r1, #8]
	.loc 3 588 0
	ldrh	r3, [r7, #88]
	mul	r3, r5, r3
	.loc 3 589 0
	add	r5, r5, #1
	uxth	r5, r5
	.loc 3 588 0
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r7, #3768]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L491:
	add	r8, r8, #1
	b	.L489
.L496:
	.loc 3 598 0
	mul	r3, r2, r9
	ldr	r1, [r10, #3760]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L495
	.loc 3 600 0
	ldr	r0, [r0, #4]
	.loc 3 599 0
	add	r4, r4, #1
.LVL451:
	str	r2, [fp, #-68]
	uxth	r4, r4
.LVL452:
	.loc 3 600 0
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL453:
	ldr	r2, [fp, #-68]
.L495:
	add	r9, r9, #1
	b	.L494
.LVL454:
.L500:
	.loc 3 611 0
	cmp	r6, #0
	beq	.L499
	.loc 3 611 0 is_stmt 0 discriminator 1
	mul	r3, r9, r7
	ldr	r2, [r10, #3760]
.LVL455:
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L499
	.loc 3 612 0 is_stmt 1
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.LVL456:
.L499:
	add	r7, r7, #1
	b	.L498
.L520:
	.align	2
.L519:
	.word	.LANCHOR0
.LFE149:
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
.LFB265:
	.loc 1 156 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL457:
	.movsp ip
	mov	ip, sp
.LCFI208:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI209:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI210:
	.pad #88
	sub	sp, sp, #88
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 156 0
	.loc 1 161 0
	cmp	r0, #11
	.loc 1 156 0
	mov	r4, r0
	.loc 1 162 0
	movls	r4, #0
	.loc 1 161 0
	bls	.L521
.LBB313:
.LBB314:
	.loc 1 165 0
	ldr	r5, .L524
	.loc 1 166 0
	sub	r0, fp, #84
.LVL458:
.LBB315:
.LBB316:
.LBB317:
	.loc 1 202 0
	mov	r2, #32
	mov	r1, #165
.LBE317:
.LBE316:
.LBE315:
	.loc 1 166 0
	str	r0, [fp, #-92]
.LVL459:
	.loc 1 169 0
	lsl	r4, r4, #10
.LVL460:
	.loc 1 165 0
	str	r5, [fp, #-96]
.LBB320:
.LBB319:
.LBB318:
	.loc 1 202 0
	bl	memset
.LVL461:
.LBE318:
.LBE319:
.LBE320:
.LBB321:
.LBB322:
.LBB323:
	ldr	r3, .L524+4
.LBE323:
.LBE322:
.LBE321:
	.loc 1 170 0
	mov	r2, #1
	mov	r1, r2
	sub	r0, fp, #104
	.loc 1 169 0
	str	r4, [fp, #-100]
.LBB326:
.LBB325:
.LBB324:
	.loc 1 202 0
	str	r3, [r5]
	str	r3, [r5, #4]
.LBE324:
.LBE325:
.LBE326:
	.loc 1 170 0
	bl	FlashEraseBlocks
.LVL462:
	.loc 1 171 0
	mov	r3, #1
	sub	r0, fp, #104
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL463:
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
.LVL464:
.L521:
.LBE314:
.LBE313:
	.loc 1 180 0
	mov	r0, r4
.LVL465:
	sub	sp, fp, #20
.LCFI211:
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L525:
	.align	2
.L524:
	.word	ftl_temp_buf
	.word	1515870810
.LFE265:
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashGetBadBlockList
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
.LFB266:
	.loc 1 183 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL466:
	.movsp ip
	mov	ip, sp
.LCFI212:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI213:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI214:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 183 0
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
	mov	r1, #255
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
	ldr	r3, .L533
	mov	r1, r5
	mov	r0, r4
	ldr	r3, [r3]
	blx	r3
.LVL469:
	uxth	r0, r0
.LVL470:
	.loc 1 188 0
	cmp	r0, #50
	bls	.L527
.LBB346:
.LBB347:
.LBB348:
	.loc 1 202 0
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
.LVL471:
	bl	memset
.LVL472:
.LBE348:
.LBE347:
.LBE346:
	.loc 1 190 0
	mov	r0, #0
.LVL473:
.L527:
	.loc 1 192 0
	ldr	r3, .L533+4
.LVL474:
	ldrh	r3, [r3, #14]
	cmp	r3, #4
	moveq	r3, r4
.LVL475:
	addeq	r1, r3, r0, lsl #1
	beq	.L529
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L530:
	.loc 1 194 0 discriminator 3
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L529:
	.loc 1 193 0 discriminator 1
	cmp	r3, r1
	bne	.L530
.LVL476:
	.loc 1 198 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L534:
	.align	2
.L533:
	.word	g_nand_ops
	.word	g_nand_phy_info
.LFE266:
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	ftl_memset
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memset, %function
ftl_memset:
	.fnstart
.LFB267:
	.loc 1 201 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL477:
	.movsp ip
	mov	ip, sp
.LCFI215:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI216:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI217:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 201 0
.LBB353:
	.loc 1 202 0
	cmp	r2, #0
.LBE353:
	.loc 1 201 0
	mov	r4, r0
.LBB354:
	.loc 1 202 0
	beq	.L536
.LBE354:
.LBB355:
.LBB356:
	bl	memset
.LVL478:
.L536:
.LBE356:
.LBE355:
	.loc 1 203 0 discriminator 8
	mov	r0, r4
	ldmfd	sp, {r4, r5, fp, sp, pc}
.LFE267:
	.fnend
	.size	ftl_memset, .-ftl_memset
	.align	2
	.global	FtlMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
.LFB146:
	.loc 3 279 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI218:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI219:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI220:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 279 0
	.loc 3 297 0
	movw	r3, #65535
	.loc 3 301 0
	movw	r2, #3182
	.loc 3 282 0
	ldr	r4, .L641
	mov	r5, #0
	.loc 3 311 0
	mov	r6, #12
	.loc 3 312 0
	mov	r7, #20
	.loc 3 297 0
	str	r3, [r4, #3784]
	.loc 3 301 0
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	.loc 3 302 0
	add	r2, r4, #3184
	strh	r3, [r2]	@ movhi
	.loc 3 303 0
	mov	r2, #32
	movw	r3, #3128
	.loc 3 310 0
	ldrh	r0, [r4, #82]
	.loc 3 303 0
	strh	r2, [r4, r3]	@ movhi
	.loc 3 304 0
	mov	r2, #128
	movw	r3, #3130
	.loc 3 282 0
	str	r5, [r4, #2472]
	.loc 3 304 0
	strh	r2, [r4, r3]	@ movhi
	.loc 3 305 0
	add	r3, r4, #3136
	strh	r5, [r3]	@ movhi
	.loc 3 306 0
	movw	r3, #3186
	strh	r5, [r4, r3]	@ movhi
	.loc 3 310 0
	lsl	r0, r0, #1
	.loc 3 307 0
	movw	r3, #3222
	.loc 3 283 0
	str	r5, [r4, #2476]
	.loc 3 307 0
	strh	r5, [r4, r3]	@ movhi
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
	.loc 3 310 0
	bl	ftl_malloc
.LVL479:
	str	r0, [r4, #3168]
	.loc 3 311 0
	ldrh	r0, [r4, #82]
	mul	r0, r6, r0
	bl	ftl_malloc
.LVL480:
	.loc 3 312 0
	ldrh	r3, [r4, #8]
	.loc 3 311 0
	str	r0, [r4, #3176]
	.loc 3 312 0
	mul	r7, r7, r3
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
	lsl	r3, r3, #1
	.loc 3 321 0
	mov	r0, r7
	.loc 3 320 0
	add	r3, r3, #1
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
	mul	r0, r0, r7
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
	mul	r0, r6, r0
	bl	ftl_malloc
.LVL492:
	.loc 3 329 0
	ldrh	r3, [r4, #88]
	ldrh	r7, [r4, #8]
	.loc 3 327 0
	str	r0, [r4, #3148]
	.loc 3 329 0
	mul	r7, r7, r3
	.loc 3 330 0
	mov	r0, r7
	bl	ftl_malloc
.LVL493:
	str	r0, [r4, #3244]
	.loc 3 331 0
	lsl	r0, r7, #2
	.loc 3 335 0
	movw	r7, #3812
	.loc 3 331 0
	bl	ftl_malloc
.LVL494:
	.loc 3 332 0
	ldrh	r3, [r4, #88]
	.loc 3 331 0
	str	r0, [r4, #3772]
	.loc 3 332 0
	ldr	r0, [r4, #3164]
	mul	r0, r0, r3
	bl	ftl_malloc
.LVL495:
	str	r0, [r4, #3156]
	.loc 3 335 0
	ldrh	r0, [r4, #18]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, r7]	@ movhi
	.loc 3 336 0
	bl	ftl_malloc
.LVL496:
	str	r0, [r4, #3816]
	.loc 3 337 0
	ldrh	r0, [r4, r7]
	.loc 3 338 0
	ldr	r3, .L641+4
	.loc 3 337 0
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r4, r7]	@ movhi
	.loc 3 338 0
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
.LVL497:
	.loc 3 345 0
	ldrh	r7, [r4, #18]
	.loc 3 338 0
	str	r0, [r4, #3820]
	.loc 3 339 0
	add	r0, r0, #32
	str	r0, [r4, #2248]
	.loc 3 345 0
	lsl	r7, r7, #1
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
	lsl	r7, r7, #1
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
	lsr	r0, r0, #3
	add	r0, r0, #4
	bl	ftl_malloc
.LVL502:
	str	r0, [r4]
	.loc 3 354 0
	ldrh	r0, [r4, #96]
	lsl	r0, r0, #1
	bl	ftl_malloc
.LVL503:
	str	r0, [r4, #124]
	.loc 3 355 0
	ldrh	r0, [r4, #96]
	lsl	r0, r0, #1
	bl	ftl_malloc
.LVL504:
	str	r0, [r4, #3836]
	.loc 3 356 0
	ldrh	r0, [r4, #96]
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL505:
	str	r0, [r4, #3840]
	.loc 3 357 0
	ldrh	r0, [r4, #98]
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL506:
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
.LVL507:
	.loc 3 360 0
	ldrh	r0, [r4, #112]
	.loc 3 361 0
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL508:
	str	r0, [r4, #3848]
	.loc 3 362 0
	ldr	r0, [r4, #104]
	.loc 3 363 0
	lsl	r0, r0, #2
	bl	ftl_malloc
.LVL509:
	str	r0, [r4, #3852]
	.loc 3 364 0
	ldrh	r0, [r4, #114]
	.loc 3 365 0
	mul	r0, r6, r0
	bl	ftl_malloc
.LVL510:
	.loc 3 366 0
	ldrh	r3, [r4, #114]
	.loc 3 365 0
	str	r0, [r4, #2424]
	.loc 3 366 0
	ldrh	r0, [r4, #86]
	.loc 3 367 0
	mul	r0, r0, r3
	bl	ftl_malloc
.LVL511:
	.loc 3 368 0
	ldrh	r3, [r4, #18]
	.loc 3 367 0
	str	r0, [r4, #3856]
	.loc 3 369 0
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
.LVL512:
	str	r0, [r4, #2252]
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
.LVL513:
	.loc 3 374 0
	ldrh	r2, [r4, r5]
	add	ip, r4, #156
	.loc 3 373 0
	ldrh	lr, [r4, #30]
	mov	r3, #1
.LVL514:
	.loc 3 372 0
	str	r0, [r4, #156]
	lsl	r2, r2, #2
	.loc 3 373 0
	mov	r1, r2
.L541:
	.loc 3 373 0 is_stmt 0 discriminator 1
	cmp	r3, lr
	bcc	.L542
	ldr	r2, .L641+8
	.loc 3 379 0 is_stmt 1
	mov	r1, #0
	add	r3, r2, r3, lsl #2
.LVL515:
	add	r2, r2, #56
	add	r3, r3, #24
.L543:
	.loc 3 377 0 discriminator 1
	cmp	r2, r3
	bne	.L544
	.loc 3 383 0
	ldr	r3, [r4, #3828]
	cmp	r3, #0
	bne	.L545
.L547:
	.loc 3 385 0
	ldr	r1, .L641+12
	ldr	r0, .L641+16
	bl	printk
.LVL516:
	.loc 3 386 0
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL517:
.L542:
	.loc 3 374 0 discriminator 3
	ldr	r0, [r4, #156]
	.loc 3 373 0 discriminator 3
	add	r3, r3, #1
	.loc 3 374 0 discriminator 3
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L541
.LVL518:
.L544:
	.loc 3 379 0 discriminator 2
	str	r1, [r3, #4]!
	b	.L543
.L545:
	.loc 3 388 0
	ldr	r3, [r4, #3832]
	cmp	r3, #0
	beq	.L547
	.loc 3 393 0
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L547
	.loc 3 398 0
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L547
	.loc 3 404 0
	ldr	r3, [r4, #2424]
	cmp	r3, #0
	beq	.L547
	.loc 3 409 0
	ldr	r3, [r4, #3856]
	cmp	r3, #0
	beq	.L547
	.loc 3 414 0
	ldr	r3, [r4, #2252]
	cmp	r3, #0
	beq	.L547
	.loc 3 419 0
	ldr	r3, [r4, #156]
	cmp	r3, #0
	beq	.L547
	.loc 3 425 0
	ldr	r3, [r4, #2260]
	cmp	r3, #0
	beq	.L547
	.loc 3 430 0
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L547
	.loc 3 430 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L547
	.loc 3 435 0 is_stmt 1
	ldr	r3, [r4, #3792]
	cmp	r3, #0
	beq	.L547
	.loc 3 435 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3800]
	cmp	r3, #0
	beq	.L547
	.loc 3 435 0 discriminator 2
	ldr	r3, [r4, #3760]
	cmp	r3, #0
	beq	.L547
	.loc 3 435 0 discriminator 3
	ldr	r3, [r4, #3160]
	cmp	r3, #0
	beq	.L547
	.loc 3 435 0 discriminator 4
	ldr	r3, [r4, #3796]
	cmp	r3, #0
	beq	.L547
	.loc 3 440 0 is_stmt 1
	ldr	r3, [r4, #2244]
	cmp	r3, #0
	beq	.L547
	.loc 3 440 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L547
	.loc 3 440 0 discriminator 2
	ldr	r3, [r4, #3808]
	cmp	r3, #0
	beq	.L547
	.loc 3 440 0 discriminator 3
	ldr	r3, [r4, #3152]
	cmp	r3, #0
	beq	.L547
	.loc 3 441 0 is_stmt 1
	ldr	r3, [r4, #3776]
	cmp	r3, #0
	beq	.L547
	.loc 3 441 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3768]
	cmp	r3, #0
	beq	.L547
	.loc 3 441 0 discriminator 2
	ldr	r3, [r4, #3148]
	cmp	r3, #0
	beq	.L547
	.loc 3 446 0 is_stmt 1
	ldr	r3, [r4, #3244]
	cmp	r3, #0
	beq	.L547
	.loc 3 446 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3772]
	cmp	r3, #0
	beq	.L547
	.loc 3 446 0 discriminator 2
	ldr	r3, [r4, #3156]
	cmp	r3, #0
	beq	.L547
	.loc 3 452 0 is_stmt 1
	ldr	r3, [r4, #2248]
	cmp	r3, #0
	beq	.L547
	.loc 3 452 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L547
	.loc 3 458 0 is_stmt 1
	ldr	r3, [r4, #124]
	cmp	r3, #0
	beq	.L547
	.loc 3 464 0
	ldr	r3, .L641
	ldr	r2, [r3, #3836]
	cmp	r2, #0
	beq	.L547
	.loc 3 470 0
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L547
	.loc 3 476 0
	ldr	r3, [r3, #3844]
	cmp	r3, #0
	beq	.L547
	.loc 3 483 0
	mov	r0, #0
	.loc 3 484 0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L642:
	.align	2
.L641:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+128
	.word	.LANCHOR1+186
	.word	.LC78
.LFE146:
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
.LFB165:
	.loc 4 66 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL519:
	.movsp ip
	mov	ip, sp
.LCFI221:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI222:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI223:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 66 0
	.loc 4 69 0
	movw	r3, #3860
	.loc 4 66 0
	mov	r4, r0
	.loc 4 69 0
	ldr	r6, .L649
	sub	r5, r4, #2
	.loc 4 74 0
	ldr	r8, .L649+4
	add	r4, r4, #1020
	ldr	r9, .L649+8
	.loc 4 66 0
	mov	r7, r1
	.loc 4 69 0
	ldrh	r2, [r6, r3]
	add	r4, r4, #2
	mov	r1, #0
.LVL520:
	mov	r0, r7
.LVL521:
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL522:
.L646:
	.loc 4 72 0
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	.loc 4 74 0
	ldrh	r2, [r6, #74]
	cmp	r2, r3
	bhi	.L645
	.loc 4 74 0 is_stmt 0 discriminator 1
	mov	r2, #74
	mov	r1, r8
	mov	r0, r9
	bl	printk
.LVL523:
.L645:
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
	bne	.L646
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L650:
	.align	2
.L649:
	.word	.LANCHOR0
	.word	.LANCHOR1+197
	.word	.LC1
.LFE165:
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	load_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
.LFB193:
	.loc 2 442 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL524:
	.movsp ip
	mov	ip, sp
.LCFI224:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI225:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI226:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 442 0
	mov	r6, r0
	mov	r8, r1
	.loc 2 446 0
	ldr	r4, .L657
	ldrh	r3, [r4, #112]
	cmp	r3, r0
	bcs	.L652
	.loc 2 446 0 is_stmt 0 discriminator 1
	movw	r2, #446
	ldr	r1, .L657+4
	ldr	r0, .L657+8
	bl	printk
.LVL525:
.L652:
	.loc 2 447 0 is_stmt 1
	ldr	r3, [r4, #3848]
	mov	r5, #12
	ldr	r7, [r3, r6, lsl #2]
.LVL526:
	.loc 2 449 0
	cmp	r7, #0
	bne	.L653
	.loc 2 450 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2424]
	ldrh	r2, [r4, #86]
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL527:
	.loc 2 451 0
	ldr	r3, [r4, #2424]
	strh	r6, [r3, r5]	@ movhi
	.loc 2 452 0
	ldr	r3, [r4, #2424]
	add	r5, r3, r5
	str	r7, [r5, #4]
.LVL528:
.L654:
	.loc 2 473 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL529:
.L653:
	.loc 2 456 0
	mul	r5, r5, r8
	ldr	r3, [r4, #2424]
	.loc 2 458 0
	mov	r2, #1
	ldr	r0, .L657+12
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
.LVL530:
	.loc 2 459 0
	ldr	r8, [r4, #3236]
.LVL531:
	.loc 2 461 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L655
	.loc 2 462 0
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L657+16
	bl	printk
.LVL532:
	.loc 2 463 0
	mov	r3, #4
	ldr	r1, [r4, #3236]
	mov	r2, r3
	ldr	r0, .L657+20
	bl	rknand_print_hex
.LVL533:
	.loc 2 464 0
	ldrh	r3, [r4, #112]
	mov	r2, #4
	ldr	r1, [r4, #3848]
	ldr	r0, .L657+24
	bl	rknand_print_hex
.LVL534:
.L655:
	.loc 2 467 0
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L656
	.loc 2 467 0 is_stmt 0 discriminator 1
	movw	r2, #467
	ldr	r1, .L657+4
	ldr	r0, .L657+8
	bl	printk
.LVL535:
.L656:
	.loc 2 470 0 is_stmt 1
	ldr	r3, [r4, #2424]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	.loc 2 471 0
	strh	r6, [r3, r5]	@ movhi
	.loc 2 472 0
	b	.L654
.L658:
	.align	2
.L657:
	.word	.LANCHOR0
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0+3224
	.word	.LC79
	.word	.LC80
	.word	.LC81
.LFE193:
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
.LFB194:
	.loc 2 476 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL536:
	.movsp ip
	mov	ip, sp
.LCFI227:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI228:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI229:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 476 0
	.loc 2 485 0
	ldrh	r2, [r0, #10]
	.loc 2 476 0
	mov	r4, r0
	.loc 2 479 0
	ldr	r5, [r0, #20]
.LVL537:
	.loc 2 485 0
	mov	r1, #0
	.loc 2 477 0
	ldr	r9, [r0, #12]
.LVL538:
	.loc 2 478 0
	ldr	r6, [r0, #24]
.LVL539:
	.loc 2 485 0
	lsl	r2, r2, #1
	mov	r0, r5
.LVL540:
	bl	ftl_memset
.LVL541:
	.loc 2 486 0
	mov	r2, #0
.LVL542:
.L660:
	.loc 2 486 0 is_stmt 0 discriminator 1
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L664
	.loc 2 494 0 is_stmt 1
	ldrh	r3, [r5]
.LVL543:
	.loc 2 497 0
	mov	r7, #0
	.loc 2 498 0
	ldr	r2, .L673
	.loc 2 495 0
	mov	r10, r7
.LVL544:
.L665:
	.loc 2 497 0 discriminator 1
	ldrh	r0, [r4, #10]
.LVL545:
	uxth	r1, r7
	cmp	r0, r1
	bhi	.L669
	.loc 2 513 0
	mov	r0, r10
	sub	sp, fp, #40
.LCFI230:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL546:
.L664:
.LCFI231:
	.loc 2 487 0
	uxth	r3, r2
	.loc 2 488 0
	mov	r1, #0
	.loc 2 487 0
	ldr	r0, [r6, r3, lsl #2]
.LVL547:
	ubfx	r0, r0, #10, #16
.LVL548:
.L661:
	.loc 2 488 0 discriminator 1
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L660
.L663:
	.loc 2 489 0
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r9, r3]
	cmp	ip, r0
	.loc 2 490 0
	ldrheq	ip, [r5, r3]
	addeq	ip, ip, #1
	strheq	ip, [r5, r3]	@ movhi
	b	.L661
.LVL549:
.L669:
	.loc 2 498 0
	ldrh	r0, [r4]
	uxth	r6, r7
.LVL550:
	cmp	r0, r1
	bne	.L666
	.loc 2 498 0 is_stmt 0 discriminator 1
	ldrh	ip, [r4, #2]
	ldrh	r0, [r2, #80]
	cmp	ip, r0
	.loc 2 499 0 is_stmt 1 discriminator 1
	lslcc	ip, r6, #1
	strhcc	r0, [r5, ip]	@ movhi
.L666:
	.loc 2 500 0
	lsl	r6, r6, #1
	ldrh	r8, [r5, r6]
	cmp	r3, r8
	movhi	r10, r1
	movhi	r3, r8
	.loc 2 504 0
	cmp	r8, #0
	bne	.L668
	.loc 2 504 0 is_stmt 0 discriminator 1
	ldrh	r0, [r9, r6]
	cmp	r0, #0
	beq	.L668
	.loc 2 506 0 is_stmt 1
	mov	r1, #1
	str	r2, [fp, #-52]
	str	r3, [fp, #-48]
	bl	FtlFreeSysBlkQueueIn
.LVL551:
	.loc 2 507 0
	strh	r8, [r9, r6]	@ movhi
	.loc 2 508 0
	ldr	r2, [fp, #-52]
	ldrh	r1, [r4, #8]
	ldr	r3, [fp, #-48]
.LVL552:
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L668:
	add	r7, r7, #1
	b	.L665
.L674:
	.align	2
.L673:
	.word	.LANCHOR0
.LFE194:
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
.LFB197:
	.loc 2 583 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL553:
	.movsp ip
	mov	ip, sp
.LCFI232:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI233:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI234:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 583 0
	.loc 2 591 0
	movw	r2, #65535
	.loc 2 583 0
	mov	r4, r0
	.loc 2 591 0
	ldrh	r3, [r0]
	.loc 2 586 0
	ldr	r5, [r0, #12]
.LVL554:
	.loc 2 591 0
	cmp	r3, r2
	bne	.L676
	.loc 2 592 0
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L677
	.loc 2 592 0 is_stmt 0 discriminator 1
	mov	r2, #592
	ldr	r1, .L685
	ldr	r0, .L685+4
	bl	printk
.LVL555:
.L677:
	.loc 2 593 0 is_stmt 1
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 594 0
	bl	FtlFreeSysBlkQueueOut
.LVL556:
	.loc 2 595 0
	mov	r3, #0
	.loc 2 594 0
	strh	r0, [r5]	@ movhi
	.loc 2 595 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 596 0
	strh	r3, [r4]	@ movhi
	.loc 2 597 0
	ldr	r3, [r4, #28]
	add	r3, r3, #1
	str	r3, [r4, #28]
.LVL557:
.L678:
	.loc 2 628 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL558:
.L676:
	.loc 2 601 0
	lsl	r3, r3, #1
	.loc 2 607 0
	ldr	r2, [r0, #28]
	.loc 2 587 0
	ldr	r6, [r0, #24]
.LVL559:
	.loc 2 612 0
	mov	r1, #255
	.loc 2 601 0
	ldrh	r7, [r5, r3]
	.loc 2 602 0
	ldrh	r3, [r0, #2]
	ldr	r5, .L685+8
.LVL560:
	orr	r3, r3, r7, lsl #10
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
	ldr	r2, .L685+12
	strh	r2, [r3, #8]	@ movhi
	.loc 2 609 0
	ldrh	r2, [r0, #4]
	.loc 2 610 0
	strh	r7, [r3, #2]	@ movhi
	.loc 2 609 0
	strh	r2, [r3]	@ movhi
	.loc 2 612 0
	ldrh	r2, [r5, #80]
	ldr	r0, [r5, #2244]
.LVL561:
	lsl	r2, r2, #3
	bl	ftl_memset
.LVL562:
	.loc 2 614 0
	mov	r2, #0
	.loc 2 613 0
	mov	r3, r2
.LVL563:
.L679:
	.loc 2 614 0 discriminator 1
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L681
	.loc 2 621 0
	mov	r2, #1
	mov	r3, #0
.LVL564:
	mov	r1, r2
	ldr	r0, .L685+16
	bl	FlashProgPages
.LVL565:
	.loc 2 622 0
	ldrh	r3, [r4, #2]
	.loc 2 626 0
	mov	r0, r4
	.loc 2 622 0
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	.loc 2 626 0
	bl	ftl_map_blk_gc
.LVL566:
	.loc 2 627 0
	b	.L678
.LVL567:
.L681:
	uxth	r1, r2
	.loc 2 615 0
	ldr	r0, [r6, r1, lsl #2]
	cmp	r7, r0, lsr #10
	bne	.L680
	.loc 2 617 0
	ldr	r0, [r5, #2244]
	.loc 2 616 0
	add	r3, r3, #1
.LVL568:
	uxth	r3, r3
.LVL569:
	.loc 2 617 0
	str	r1, [r0, r3, lsl #3]
	.loc 2 618 0
	ldr	r0, [r6, r1, lsl #2]
	ldr	r1, [r5, #2244]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L680:
	add	r2, r2, #1
	b	.L679
.L686:
	.align	2
.L685:
	.word	.LANCHOR1+227
	.word	.LC1
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3224
.LFE197:
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
.LFB198:
	.loc 2 631 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL570:
	.movsp ip
	mov	ip, sp
.LCFI235:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI236:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI237:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 631 0
	.loc 2 634 0
	mov	r6, #0
.LVL571:
	.loc 2 631 0
	mov	r4, r0
	.loc 2 637 0
	ldr	r7, .L706
	.loc 2 631 0
	mov	r8, r1
	mov	r9, r2
	mov	r5, r7
.L688:
	.loc 2 637 0
	ldr	r3, [r7, #2456]
	add	r3, r3, #1
	str	r3, [r7, #2456]
	.loc 2 638 0
	ldrh	r3, [r7, #80]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L689
	.loc 2 638 0 is_stmt 0 discriminator 1
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L690
.L689:
	.loc 2 639 0 is_stmt 1
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.LVL572:
.L690:
	.loc 2 650 0
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L691
	.loc 2 650 0 is_stmt 0 discriminator 1
	movw	r2, #650
	ldr	r1, .L706+4
	ldr	r0, .L706+8
	bl	printk
.LVL573:
.L691:
	.loc 2 651 0 is_stmt 1
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L692
	.loc 2 651 0 is_stmt 0 discriminator 1
	movw	r2, #651
	ldr	r1, .L706+4
	ldr	r0, .L706+8
	bl	printk
.LVL574:
.L692:
	.loc 2 652 0 is_stmt 1
	ldrh	r3, [r4]
.LBB362:
.LBB363:
.LBB364:
	.loc 1 202 0
	mov	r1, #16
.LBE364:
.LBE363:
.LBE362:
	.loc 2 652 0
	ldr	r2, [r4, #12]
	.loc 2 656 0
	ldr	r0, [r5, #3244]
	.loc 2 652 0
	lsl	r3, r3, #1
	ldrh	r10, [r2, r3]
	.loc 2 654 0
	ldrh	r3, [r4, #2]
	.loc 2 656 0
	str	r0, [r5, #3236]
	.loc 2 655 0
	str	r9, [r5, #3232]
	.loc 2 654 0
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #3228]
.LBB367:
.LBB366:
.LBB365:
	.loc 1 202 0
	bl	__memzero
.LVL575:
.LBE365:
.LBE366:
.LBE367:
	.loc 2 659 0
	ldr	r3, [r5, #3236]
.LVL576:
	.loc 2 660 0
	ldr	r2, [r4, #28]
	.loc 2 664 0
	ldr	r0, .L706+12
	.loc 2 661 0
	strh	r8, [r3, #8]	@ movhi
	.loc 2 660 0
	str	r2, [r3, #4]
	.loc 2 662 0
	ldrh	r2, [r4, #4]
	.loc 2 663 0
	strh	r10, [r3, #2]	@ movhi
	.loc 2 662 0
	strh	r2, [r3]	@ movhi
	.loc 2 664 0
	mov	r3, #1
.LVL577:
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL578:
	.loc 2 665 0
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	.loc 2 667 0
	ldr	r2, [r5, #3224]
	cmn	r2, #1
	bne	.L693
	.loc 2 668 0
	ldr	r1, [r5, #3228]
	.loc 2 669 0
	add	r6, r6, #1
.LVL579:
	.loc 2 668 0
	ldr	r0, .L706+16
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
	ldrhls	r3, [r5, #80]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	.loc 2 673 0
	cmp	r6, #3
	bls	.L695
	.loc 2 674 0
	mov	r2, r6
	ldr	r1, [r5, #3228]
	ldr	r0, .L706+20
	bl	printk
.LVL582:
.L696:
	b	.L696
.L695:
	.loc 2 678 0
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L688
.L705:
	b	.L705
.L693:
	.loc 2 682 0
	cmp	r3, #1
	beq	.L688
	.loc 2 685 0
	ldr	r2, [r5, #3228]
	.loc 2 687 0
	mov	r0, #0
	.loc 2 685 0
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	.loc 2 687 0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L707:
	.align	2
.L706:
	.word	.LANCHOR0
	.word	.LANCHOR1+258
	.word	.LC1
	.word	.LANCHOR0+3224
	.word	.LC82
	.word	.LC83
.LFE198:
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
.LFB196:
	.loc 2 537 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL583:
	.movsp ip
	mov	ip, sp
.LCFI238:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI239:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI240:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 537 0
	mov	r4, r0
	.loc 2 541 0
	ldr	r6, [r0, #12]
.LVL584:
	.loc 2 542 0
	ldr	r9, [r0, #24]
.LVL585:
	.loc 2 545 0
	bl	ftl_free_no_use_map_blk
.LVL586:
	.loc 2 547 0
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L720
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L709
	.loc 2 548 0
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r6, r0]
.LVL587:
	.loc 2 549 0
	cmp	r8, #0
	beq	.L709
	.loc 2 549 0 is_stmt 0 discriminator 1
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L709
	.loc 2 550 0 is_stmt 1
	mov	r2, #1
	str	r2, [r4, #32]
	.loc 2 551 0
	strh	r3, [r6, r0]	@ movhi
	.loc 2 552 0
	ldrh	r3, [r4, #8]
	.loc 2 553 0
	ldrh	r2, [r4, #2]
	.loc 2 552 0
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	.loc 2 553 0
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L710
	.loc 2 554 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL588:
.L710:
	.loc 2 537 0 discriminator 1
	mov	r6, #0
.LVL589:
.L711:
	.loc 2 556 0 discriminator 1
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L715
	.loc 2 572 0
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.LVL590:
	.loc 2 573 0
	mov	r3, #0
	str	r3, [r4, #32]
.LVL591:
.L709:
	.loc 2 577 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bcc	.L716
	.loc 2 578 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL592:
.L716:
	.loc 2 580 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI241:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL593:
.L715:
.LCFI242:
	uxth	r7, r6
	.loc 2 557 0
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L712
	.loc 2 558 0
	ldr	r3, [r5, #3804]
	.loc 2 562 0
	ldr	r0, .L720+4
	.loc 2 558 0
	str	r3, [r5, #3232]
	.loc 2 559 0
	ldr	r3, [r5, #3244]
	str	r3, [r5, #3236]
	.loc 2 560 0
	ldr	r2, [r9, r7, lsl #2]
	.loc 2 559 0
	str	r3, [fp, #-48]
	.loc 2 560 0
	str	r2, [r5, #3228]
	.loc 2 562 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL594:
	.loc 2 564 0
	ldr	r3, [fp, #-48]
	ldrh	r3, [r3, #8]
	cmp	r3, r10
	beq	.L713
	.loc 2 564 0 is_stmt 0 discriminator 1
	mov	r2, #564
	ldr	r1, .L720+8
	ldr	r0, .L720+12
	bl	printk
.LVL595:
.L713:
	.loc 2 565 0 is_stmt 1
	ldr	r3, [r5, #3224]
	cmn	r3, #1
	.loc 2 566 0
	moveq	r3, #0
	streq	r3, [r9, r7, lsl #2]
	.loc 2 565 0
	beq	.L712
	.loc 2 568 0
	ldr	r2, [r5, #3232]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.LVL596:
.L712:
	add	r6, r6, #1
	b	.L711
.L721:
	.align	2
.L720:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
	.word	.LANCHOR1+274
	.word	.LC1
.LFE196:
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	flush_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
.LFB199:
	.loc 2 690 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL597:
	.movsp ip
	mov	ip, sp
.LCFI243:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI244:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI245:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 690 0
	.loc 2 693 0
	mov	r4, #12
	ldr	r5, .L723
	mul	r4, r4, r0
	ldr	r3, [r5, #2424]
	.loc 2 694 0
	add	r0, r5, #3856
.LVL598:
	add	r0, r0, #8
	.loc 2 693 0
	add	r2, r3, r4
	.loc 2 694 0
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
.LVL599:
	.loc 2 695 0
	ldr	r3, [r5, #2424]
	.loc 2 697 0
	mov	r0, #0
	.loc 2 695 0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	.loc 2 697 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L724:
	.align	2
.L723:
	.word	.LANCHOR0
.LFE199:
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
.LFB202:
	.loc 2 753 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL600:
	.movsp ip
	mov	ip, sp
.LCFI246:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI247:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI248:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 753 0
	.loc 2 757 0
	mvn	r6, #0
	.loc 2 753 0
	mov	r7, r1
	.loc 2 756 0
	ldr	r4, .L739
	.loc 2 753 0
	mov	r10, r2
	.loc 2 756 0
	ldrh	r3, [r4, #84]
	add	r3, r3, #7
	lsr	r8, r0, r3
	.loc 2 757 0
	bic	r6, r0, r6, lsl r3
	.loc 2 759 0
	ldr	r3, [r4, #2436]
	.loc 2 756 0
	uxth	r8, r8
.LVL601:
	.loc 2 757 0
	uxth	r6, r6
.LVL602:
	.loc 2 759 0
	cmp	r0, r3
	bcc	.L726
	.loc 2 759 0 is_stmt 0 discriminator 1
	movw	r2, #759
	ldr	r1, .L739+4
	ldr	r0, .L739+8
.LVL603:
	bl	printk
.LVL604:
.L726:
	.loc 2 761 0 is_stmt 1
	ldrh	r2, [r4, #114]
	mov	r3, #0
	.loc 2 762 0
	ldr	r1, [r4, #2424]
	mov	r9, #12
.LVL605:
.L727:
	uxth	r5, r3
	.loc 2 761 0 discriminator 1
	cmp	r5, r2
	bcc	.L732
	.loc 2 776 0
	bl	select_l2p_ram_region
.LVL606:
	.loc 2 777 0
	mul	r9, r9, r0
	ldr	r3, [r4, #2424]
	.loc 2 776 0
	mov	r5, r0
	.loc 2 777 0
	ldrh	r1, [r3, r9]
	add	r2, r3, r9
	movw	r3, #65535
	cmp	r1, r3
	beq	.L733
	.loc 2 777 0 is_stmt 0 discriminator 1
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L733
	.loc 2 778 0 is_stmt 1
	bl	flush_l2p_region
.LVL607:
.L733:
	.loc 2 781 0
	mov	r1, r5
	mov	r0, r8
	bl	load_l2p_region
.LVL608:
	.loc 2 782 0
	b	.L728
.LVL609:
.L732:
	add	r3, r3, #1
	.loc 2 762 0
	mla	r0, r9, r3, r1
.LVL610:
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L727
.L728:
	.loc 2 764 0
	cmp	r10, #0
	mov	r3, #12
	bne	.L729
	.loc 2 765 0
	ldr	r2, [r4, #2424]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r7]
.L730:
	.loc 2 771 0
	ldr	r2, [r4, #2424]
	mov	r3, #12
	.loc 2 783 0
	mov	r0, #0
	.loc 2 771 0
	mla	r5, r3, r5, r2
.LVL611:
	ldr	r3, [r5, #4]
	cmn	r3, #1
	.loc 2 772 0
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	.loc 2 783 0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL612:
.L729:
	.loc 2 767 0
	mul	r3, r3, r5
	ldr	r2, [r4, #2424]
	ldr	r1, [r7]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r6, lsl #2]
	.loc 2 768 0
	ldr	r2, [r4, #2424]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	.loc 2 769 0
	movw	r3, #2428
	strh	r8, [r4, r3]	@ movhi
	b	.L730
.L740:
	.align	2
.L739:
	.word	.LANCHOR0
	.word	.LANCHOR1+289
	.word	.LC1
.LFE202:
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
.LFB220:
	.loc 2 1628 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL613:
	.movsp ip
	mov	ip, sp
.LCFI249:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, fp, ip, lr}
.LCFI250:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI251:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1628 0
	.loc 2 1631 0
	ldr	r5, .L751
	.loc 2 1628 0
	mov	r7, r0
	.loc 2 1629 0
	ubfx	r0, r1, #10, #16
.LVL614:
	.loc 2 1628 0
	str	r1, [fp, #-40]
	.loc 2 1629 0
	bl	P2V_block_in_plane
.LVL615:
	.loc 2 1631 0
	ldr	r2, [r5, #2260]
.LVL616:
	lsl	r6, r0, #1
	ldrh	r3, [r2, r6]
	cmp	r3, #0
	bne	.L742
	.loc 2 1632 0
	ldr	r4, [r5, #2272]
	cmp	r4, #0
	beq	.L743
.LBB368:
	.loc 2 1635 0
	movw	r2, #2276
	.loc 2 1636 0
	ldr	ip, .L751+4
	.loc 2 1635 0
	ldrh	r1, [r5, r2]
.LVL617:
	.loc 2 1644 0
	mov	lr, #6
	.loc 2 1636 0
	ldr	r2, [r5, #2252]
	.loc 2 1644 0
	movw	r8, #65535
	.loc 2 1636 0
	sub	r4, r4, r2
	asr	r4, r4, #1
	mul	r4, ip, r4
	uxth	r4, r4
.LVL618:
.L744:
	.loc 2 1637 0 discriminator 1
	uxth	ip, r3
	cmp	r1, ip
	bls	.L743
	.loc 2 1638 0
	cmp	r4, r0
	bne	.L745
	.loc 2 1639 0
	mov	r1, r4
.LVL619:
	ldr	r0, .L751+8
.LVL620:
	bl	List_remove_node
.LVL621:
	movw	r3, #2276
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L746
	.loc 2 1639 0 is_stmt 0 discriminator 1
	movw	r2, #1639
	ldr	r1, .L751+12
	ldr	r0, .L751+16
	bl	printk
.LVL622:
.L746:
	.loc 2 1639 0 discriminator 3
	movw	r2, #2276
	.loc 2 1640 0 is_stmt 1 discriminator 3
	mov	r0, r4
	.loc 2 1639 0 discriminator 3
	ldrh	r3, [r5, r2]
	sub	r3, r3, #1
	strh	r3, [r5, r2]	@ movhi
	.loc 2 1640 0 discriminator 3
	bl	INSERT_DATA_LIST
.LVL623:
	.loc 2 1641 0 discriminator 3
	ldr	r2, [r5, #2260]
	ldrh	r3, [r2, r6]
.LVL624:
.L742:
.LBE368:
	.loc 2 1650 0
	add	r3, r3, #1
	strh	r3, [r2, r6]	@ movhi
	b	.L743
.LVL625:
.L745:
.LBB369:
	.loc 2 1644 0
	mul	r4, lr, r4
.LVL626:
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
.LVL627:
	cmp	r4, r8
	bne	.L744
.L743:
.LBE369:
	.loc 2 1652 0
	mov	r2, #1
	sub	r1, fp, #40
.LVL628:
	mov	r0, r7
.LVL629:
	bl	log2phys
.LVL630:
	.loc 2 1653 0
	sub	sp, fp, #32
.LCFI252:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL631:
.L752:
	.align	2
.L751:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2272
	.word	.LANCHOR1+298
	.word	.LC1
.LFE220:
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
.LFB231:
	.loc 2 2275 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI253:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI254:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI255:
	.pad #32
	sub	sp, sp, #32
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2275 0
	.loc 2 2281 0
	ldr	r6, .L761
	mov	r5, #0
.LVL632:
	.loc 2 2280 0
	mov	r1, #0
	ldr	r0, .L761+4
	bl	printk
.LVL633:
	mov	r4, r6
.LVL634:
.L754:
	.loc 2 2281 0 discriminator 1
	ldr	r3, [r6, #2436]
	cmp	r5, r3
	bcc	.L760
	.loc 2 2303 0
	sub	sp, fp, #28
.LCFI256:
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL635:
.L760:
.LCFI257:
	.loc 2 2282 0
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
.LVL636:
	.loc 2 2283 0
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L755
	.loc 2 2284 0
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L761+8
	bl	printk
.LVL637:
.L755:
	.loc 2 2285 0
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L757
	.loc 2 2286 0
	str	r3, [r4, #3228]
	.loc 2 2290 0
	mov	r2, #0
	.loc 2 2288 0
	ldr	r3, [r4, #2244]
	.loc 2 2292 0
	mov	r1, #1
	.loc 2 2289 0
	ldr	r7, [r4, #3244]
	.loc 2 2292 0
	ldr	r0, .L761+12
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
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L758
	.loc 2 2294 0
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L757
.L758:
	.loc 2 2297 0
	ldr	r2, [r4, #3232]
	ldr	r3, [r4, #3236]
	ldr	r0, .L761+16
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
.L757:
	.loc 2 2281 0 discriminator 2
	add	r5, r5, #1
	b	.L754
.L762:
	.align	2
.L761:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LC85
	.word	.LANCHOR0+3224
	.word	.LC86
.LFE231:
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
.LFB258:
	.loc 5 419 0
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI258:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI259:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI260:
	.pad #88
	sub	sp, sp, #88
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 419 0
	.loc 5 425 0
	ldr	r3, .L776
	ldr	r0, [r3, #2696]
	mov	r4, r3
	cmp	r0, #0
	beq	.L764
	.loc 5 426 0
	ldr	r1, [r3, #2700]
	ldr	r2, [r3, #2436]
	cmp	r1, r2
	bcs	.L765
	mov	r5, #2048
.LVL640:
.L770:
.LBB375:
.LBB376:
	.loc 5 428 0
	ldr	r0, [r4, #2700]
	ldr	r3, [r4, #2436]
	cmp	r0, r3
	bcs	.L767
	.loc 5 430 0
	mov	r2, #0
	sub	r1, fp, #108
	bl	log2phys
.LVL641:
	.loc 5 432 0
	ldr	r2, [fp, #-108]
	.loc 5 431 0
	ldr	r3, [r4, #2700]
	.loc 5 432 0
	cmn	r2, #1
	.loc 5 431 0
	add	r3, r3, #1
	str	r3, [r4, #2700]
	.loc 5 432 0
	beq	.L768
.LBB377:
	.loc 5 435 0
	str	r3, [fp, #-88]
	.loc 5 438 0
	sub	r0, fp, #20
	.loc 5 436 0
	ldr	r3, .L776+4
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
.LVL642:
	.loc 5 440 0
	ldr	r3, [fp, #-104]
	cmp	r3, #256
	bne	.L767
	.loc 5 442 0
	ldr	r0, [fp, #-108]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL643:
	bl	FtlGcRefreshBlock
.LVL644:
.L767:
.LBE377:
	mvn	r0, #0
.L763:
.LBE376:
.LBE375:
	.loc 5 466 0
	sub	sp, fp, #20
.LCFI261:
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L768:
.LCFI262:
.LBB379:
.LBB378:
	.loc 5 427 0
	subs	r5, r5, #1
	bne	.L770
	b	.L767
.LVL645:
.L765:
.LBE378:
.LBE379:
	.loc 5 451 0
	ldr	r2, [r3, #2440]
	.loc 5 449 0
	mov	r0, #0
	str	r0, [r3, #2696]
	.loc 5 450 0
	str	r0, [r3, #2700]
	.loc 5 451 0
	str	r2, [r3, #2692]
	b	.L763
.L764:
.LBB380:
	.loc 5 456 0
	ldr	r1, [r3, #2440]
	ldr	r2, [r3, #2692]
	add	ip, r1, #1048576
	cmp	r2, ip
	bhi	.L773
	.loc 5 454 0
	ldr	r3, [r3, #2496]
	.loc 5 457 0
	mov	ip, #33554432
	.loc 5 454 0
	lsr	r3, r3, #10
	.loc 5 457 0
	add	r2, r2, ip, asr r3
	cmp	r1, r2
	bhi	.L773
	.loc 5 458 0
	ldr	r3, .L776+8
	ldrb	r3, [r3, #28]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L763
.L773:
	.loc 5 460 0
	mov	r3, #1
	.loc 5 461 0
	mov	r0, #0
	.loc 5 460 0
	str	r3, [r4, #2696]
	.loc 5 461 0
	str	r0, [r4, #2700]
	.loc 5 462 0
	str	r1, [r4, #2692]
	b	.L763
.L777:
	.align	2
.L776:
	.word	.LANCHOR0
	.word	ftl_temp_buf
	.word	.LANCHOR0+2504
.LBE380:
.LFE258:
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_check_vpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	.fnstart
.LFB230:
	.loc 2 2223 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI263:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI264:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI265:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2223 0
	.loc 2 2232 0
	ldr	r6, .L802
	mov	r4, #0
.LVL646:
	.loc 2 2229 0
	ldr	r1, .L802+4
	ldr	r0, .L802+8
	mov	r5, r6
	bl	printk
.LVL647:
.LBB387:
.LBB388:
.LBB389:
	.loc 1 202 0
	mov	r1, #8192
	ldr	r0, .L802+12
	bl	__memzero
.LVL648:
.L779:
.LBE389:
.LBE388:
.LBE387:
	.loc 2 2232 0 discriminator 1
	ldr	r3, [r6, #2436]
	cmp	r4, r3
	bcc	.L781
	.loc 2 2245 0 discriminator 1
	ldr	r8, .L802+12
	mov	r4, #0
.LVL649:
	.loc 2 2247 0 discriminator 1
	ldr	r9, .L802+16
	mov	r6, r4
.LVL650:
.L782:
	.loc 2 2243 0 discriminator 1
	ldrh	r2, [r5, #16]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L784
	.loc 2 2255 0
	ldr	r4, [r5, #2272]
	cmp	r4, #0
	beq	.L785
.LBB390:
	.loc 2 2257 0
	movw	r3, #2276
	.loc 2 2263 0
	ldr	r9, .L802+12
	.loc 2 2257 0
	ldrh	r7, [r5, r3]
.LVL651:
	.loc 2 2259 0
	mov	r8, #0
	.loc 2 2258 0
	ldr	r3, [r5, #2252]
	.loc 2 2263 0
	ldr	r10, .L802+20
	.loc 2 2258 0
	sub	r4, r4, r3
	ldr	r3, .L802+24
	asr	r4, r4, #1
	mul	r4, r3, r4
	uxth	r4, r4
.L786:
	.loc 2 2259 0 discriminator 1
	uxth	r3, r8
	cmp	r7, r3
	bls	.L785
	.loc 2 2261 0
	ldr	r2, [r5, #2260]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L787
	.loc 2 2264 0
	mov	r6, #1
	.loc 2 2263 0
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.LVL652:
.L787:
	.loc 2 2266 0
	mov	r3, #6
	ldr	r2, [r5, #2252]
	mul	r4, r3, r4
	movw	r3, #65535
	add	r8, r8, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r3
	bne	.L786
.L785:
.LBE390:
	.loc 2 2271 0
	cmp	r6, #0
	beq	.L778
	.loc 2 2271 0 is_stmt 0 discriminator 1
	movw	r2, #2271
	ldr	r1, .L802+4
	ldr	r0, .L802+28
	bl	printk
.LVL653:
.L778:
	.loc 2 2272 0 is_stmt 1
	sub	sp, fp, #40
.LCFI266:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL654:
.L781:
.LCFI267:
	.loc 2 2234 0
	mov	r2, #0
	sub	r1, fp, #48
	mov	r0, r4
	bl	log2phys
.LVL655:
	.loc 2 2235 0
	ldr	r0, [fp, #-48]
	cmn	r0, #1
	beq	.L780
	.loc 2 2237 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL656:
	.loc 2 2238 0
	ldr	r2, .L802+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L780:
	.loc 2 2232 0 discriminator 2
	add	r4, r4, #1
	b	.L779
.LVL657:
.L784:
	.loc 2 2245 0
	uxth	r1, r4
	ldr	r3, [r5, #2260]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L783
	.loc 2 2247 0
	mov	r0, r9
	bl	printk
.LVL658:
	.loc 2 2248 0
	ldr	r3, [r5, #2260]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L783
	.loc 2 2248 0 is_stmt 0 discriminator 1
	ldrh	r2, [r8, r7]
	.loc 2 2250 0 is_stmt 1 discriminator 1
	cmp	r2, r3
	movhi	r6, #1
.L783:
	add	r4, r4, #1
	b	.L782
.L803:
	.align	2
.L802:
	.word	.LANCHOR0
	.word	.LANCHOR1+314
	.word	.LC87
	.word	check_vpc_table
	.word	.LC88
	.word	.LC89
	.word	-1431655765
	.word	.LC1
.LFE230:
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	.fnstart
.LFB207:
	.loc 2 897 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL659:
	.movsp ip
	mov	ip, sp
.LCFI268:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI269:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI270:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 897 0
	mov	r6, r0
	.loc 2 903 0
	ldr	r3, [r0, #36]
	cmp	r3, #0
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	.loc 2 905 0
	mov	r3, #0
	.loc 2 899 0
	ldrh	r5, [r0, #6]
	.loc 2 905 0
	str	r3, [r0, #36]
	.loc 2 907 0
	ldr	r3, .L811
	.loc 2 900 0
	ldr	r2, [r0, #24]
.LVL660:
	.loc 2 899 0
	sub	r5, r5, #1
	.loc 2 930 0
	uxth	r5, r5
	.loc 2 907 0
	ldr	r0, [r3, #3804]
	mov	r4, r3
	.loc 2 908 0
	ldr	r1, [r3, #3244]
	.loc 2 907 0
	str	r0, [r3, #3232]
	.loc 2 908 0
	str	r1, [r3, #3236]
	.loc 2 930 0
	ldr	r2, [r2, r5, lsl #2]
.LVL661:
	.loc 2 932 0
	cmp	r2, #0
	.loc 2 931 0
	str	r2, [r3, #3228]
	.loc 2 932 0
	beq	.L806
	.loc 2 934 0
	mov	r2, #1
.LVL662:
	ldr	r0, .L811+4
	mov	r1, r2
	bl	FlashReadPages
.LVL663:
.L807:
	.loc 2 941 0
	ldr	r2, [r4, #3232]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
.LVL664:
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL665:
.L806:
	.loc 2 938 0
	ldrh	r2, [r3, #86]
.LVL666:
	mov	r1, #255
	bl	ftl_memset
.LVL667:
	b	.L807
.L812:
	.align	2
.L811:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
.LFE207:
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlScanSysBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
.LFB210:
	.loc 2 1095 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI271:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI272:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI273:
	.pad #28
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1095 0
	.loc 2 1101 0
	ldr	r4, .L895
	mov	r5, #0
	.loc 2 1103 0
	mov	r1, r5
	ldr	r2, [r4, #104]
	.loc 2 1101 0
	add	r6, r4, #3904
	.loc 2 1103 0
	ldr	r0, [r4, #3852]
	mov	r8, r4
	.loc 2 1101 0
	strh	r5, [r6]	@ movhi
	.loc 2 1102 0
	strh	r5, [r4, #120]	@ movhi
	.loc 2 1103 0
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL668:
	.loc 2 1104 0
	ldr	r2, [r4, #104]
	mov	r1, r5
	ldr	r0, [r4, #3828]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL669:
	.loc 2 1105 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #3840]
	lsl	r2, r2, #2
	bl	ftl_memset
.LVL670:
	.loc 2 1106 0
	ldrh	r2, [r4, #96]
	mov	r1, r5
	ldr	r0, [r4, #124]
	.loc 2 1109 0
	mov	r5, r4
	.loc 2 1106 0
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL671:
.LBB396:
.LBB397:
.LBB398:
	.loc 1 202 0
	ldr	r2, .L895+4
	mvn	r3, #0
	str	r3, [r4, #2556]
	str	r6, [fp, #-60]
	str	r3, [r2, #4]
	str	r3, [r2, #8]
.LBE398:
.LBE397:
.LBE396:
	.loc 2 1109 0
	ldrh	r3, [r4, #16]
.LVL672:
	str	r3, [fp, #-52]
.LVL673:
.L814:
	.loc 2 1109 0 is_stmt 0 discriminator 1
	ldrh	r3, [r5, #18]
.LVL674:
	ldr	r2, [fp, #-52]
.LVL675:
	cmp	r3, r2
	bls	.L855
	.loc 2 1120 0 is_stmt 1 discriminator 1
	mov	r6, #0
	.loc 2 1113 0 discriminator 1
	ldrh	r3, [r5, #8]
	.loc 2 1118 0 discriminator 1
	ldr	r7, [r5, #3792]
.LVL676:
	.loc 2 1119 0 discriminator 1
	ldr	r10, [r5, #3152]
	.loc 2 1120 0 discriminator 1
	ldr	r9, [r5, #3156]
	ldrh	r2, [r5, #88]
.LVL677:
	str	r6, [fp, #-48]
	b	.L856
.L816:
	str	r3, [fp, #-64]
	.loc 2 1115 0
	ldr	r3, .L895+8
	ldr	r1, [fp, #-52]
	str	r2, [fp, #-68]
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	V2P_block
.LVL678:
	str	r0, [fp, #-56]
	.loc 2 1116 0
	bl	FtlBbmIsBadBlock
.LVL679:
	cmp	r0, #0
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-68]
	bne	.L815
	.loc 2 1118 0
	ldr	r1, [fp, #-48]
.LVL680:
	mov	r0, #20
	mla	r0, r0, r1, r7
	ldr	r1, [fp, #-56]
.LVL681:
	lsl	r1, r1, #10
	.loc 2 1119 0
	stmib	r0, {r1, r10}
	.loc 2 1120 0
	ldr	r1, [fp, #-48]
.LVL682:
	mul	r1, r2, r1
.LVL683:
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	bic	r1, r1, #3
	add	r1, r9, r1
	str	r1, [r0, #12]
	.loc 2 1121 0
	ldr	r1, [fp, #-48]
.LVL684:
	add	r1, r1, #1
.LVL685:
	uxth	r1, r1
.LVL686:
	str	r1, [fp, #-48]
.L815:
	add	r6, r6, #1
.L856:
	.loc 2 1113 0 discriminator 1
	uxth	r1, r6
.LVL687:
	cmp	r3, r1
	bhi	.L816
	.loc 2 1124 0
	ldr	r3, [fp, #-48]
.LVL688:
	cmp	r3, #0
	bne	.L817
.LVL689:
.L854:
	.loc 2 1109 0
	ldr	r3, [fp, #-52]
.LVL690:
	add	r3, r3, #1
.LVL691:
	uxth	r3, r3
.LVL692:
	str	r3, [fp, #-52]
	b	.L814
.LVL693:
.L817:
	.loc 2 1127 0
	mov	r2, #1
	ldr	r1, [fp, #-48]
	mov	r0, r7
	bl	FlashReadPages
.LVL694:
	.loc 2 1128 0
	mov	r3, #0
.LVL695:
.L894:
	str	r3, [fp, #-56]
	.loc 2 1128 0 is_stmt 0 discriminator 2
	ldr	r2, [fp, #-48]
.LVL696:
	ldrh	r3, [fp, #-56]
	cmp	r2, r3
	bls	.L854
	ldr	r3, [fp, #-56]
	mov	r9, #20
	mul	r9, r9, r3
	.loc 2 1129 0 is_stmt 1
	ldr	r3, [r4, #3792]
	add	r2, r3, r9
.LVL697:
	.loc 2 1132 0
	ldr	r3, [r3, r9]
	.loc 2 1129 0
	ldr	r6, [r2, #4]
	.loc 2 1130 0
	ldr	r7, [r2, #12]
	.loc 2 1132 0
	cmn	r3, #1
	.loc 2 1129 0
	ubfx	r6, r6, #10, #16
	.loc 2 1132 0
	bne	.L821
	mov	r10, #16
	.loc 2 1136 0
	movw	r3, #65535
.L823:
	.loc 2 1134 0
	ldr	r0, [r4, #3792]
	str	r3, [fp, #-64]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	add	r2, r2, #1
	str	r2, [r0, #4]
	.loc 2 1135 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL698:
	.loc 2 1136 0
	ldrh	r2, [r7]
	ldr	r3, [fp, #-64]
	cmp	r2, r3
	bne	.L820
	.loc 2 1137 0
	ldr	r3, [r4, #3792]
	mvn	r2, #0
	str	r2, [r3, r9]
	.loc 2 1145 0
	ldr	r3, [r4, #3792]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L821
.L822:
	.loc 2 1287 0
	mov	r1, #1
	b	.L893
.L820:
	.loc 2 1139 0
	ldr	r2, [r4, #3792]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L821
	sub	r10, r10, #1
	uxth	r10, r10
	.loc 2 1133 0 discriminator 2
	cmp	r10, #0
	bne	.L823
	b	.L822
.L821:
	.loc 2 1147 0
	ldr	r2, [r5, #2472]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L824
	.loc 2 1147 0 is_stmt 0 discriminator 1
	cmp	r2, r3
	bhi	.L825
.L824:
	.loc 2 1149 0 is_stmt 1
	cmn	r3, #1
	.loc 2 1152 0
	addne	r2, r3, #1
	strne	r2, [r8, #2472]
.L825:
	.loc 2 1157 0
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L827
	bhi	.L828
	movw	r3, #61574
	cmp	r2, r3
	beq	.L829
.L826:
	ldr	r3, [fp, #-56]
	add	r3, r3, #1
	b	.L894
.L828:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L830
	movw	r3, #65535
	cmp	r2, r3
	.loc 2 1280 0
	moveq	r1, #0
	.loc 2 1157 0
	bne	.L826
.L893:
	.loc 2 1287 0
	mov	r0, r6
	bl	FtlFreeSysBlkQueueIn
.LVL699:
	b	.L826
.L830:
	.loc 2 1162 0
	ldr	r3, [fp, #-60]
	ldrh	r2, [r3]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L832
	.loc 2 1162 0 is_stmt 0 discriminator 1
	movw	r2, #1162
	ldr	r1, .L895+12
	ldr	r0, .L895+16
	bl	printk
.LVL700:
.L832:
	.loc 2 1163 0 is_stmt 1
	ldr	r0, [r4, #104]
	ldr	r2, [fp, #-60]
	.loc 2 1165 0
	ldr	ip, [r4, #3852]
	.loc 2 1163 0
	ldrh	r2, [r2]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sxth	r3, r3
	sub	r1, r1, #1
	sxth	r1, r1
.L833:
	.loc 2 1163 0 is_stmt 0 discriminator 1
	cmp	r3, r1
	bgt	.L839
	.loc 2 1186 0 is_stmt 1
	cmp	r3, #0
	bge	.L871
	b	.L826
.L839:
	.loc 2 1165 0
	ldr	r10, [r7, #4]
	lsl	lr, r3, #2
	ldr	r9, [ip, r3, lsl #2]
	cmp	r10, r9
	bls	.L834
	.loc 2 1167 0
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L835
	.loc 2 1167 0 is_stmt 0 discriminator 1
	cmp	r0, r2
	.loc 2 1172 0 is_stmt 1 discriminator 1
	ldrne	r1, .L895+20
	addne	r2, r2, #1
	strhne	r2, [r1]	@ movhi
.L835:
	.loc 2 1175 0 discriminator 1
	uxth	ip, r3
	mov	r1, #0
.L836:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L837
	.loc 2 1180 0
	ldr	r1, [r7, #4]
	.loc 2 1186 0
	cmp	r3, #0
	.loc 2 1180 0
	ldr	r2, [r5, #3852]
	str	r1, [r2, lr]
	.loc 2 1181 0
	lsl	r2, r3, #1
	ldr	r1, [r5, #3828]
	strh	r6, [r1, r2]	@ movhi
	.loc 2 1186 0
	blt	.L826
	.loc 2 1193 0
	ldr	r2, .L895+20
	ldr	r1, [r5, #104]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L826
.L871:
	.loc 2 1195 0
	ldr	r1, [fp, #-60]
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	.loc 2 1196 0
	ldr	r2, [r4, #3852]
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	.loc 2 1197 0
	lsl	r3, r3, #1
	ldr	r2, [r4, #3828]
.L891:
	.loc 2 1241 0
	strh	r6, [r2, r3]	@ movhi
	b	.L826
.L837:
	.loc 2 1177 0 discriminator 3
	ldr	r0, [r5, #3852]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	.loc 2 1178 0 discriminator 3
	lsl	r2, r2, #1
	ldr	r0, [r5, #3828]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L836
.L834:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L833
.L829:
	.loc 2 1203 0
	ldrh	r2, [r4, #120]
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bls	.L842
	.loc 2 1203 0 is_stmt 0 discriminator 1
	movw	r2, #1203
	ldr	r1, .L895+12
	ldr	r0, .L895+16
	bl	printk
.LVL701:
.L842:
	.loc 2 1204 0 is_stmt 1
	ldrh	r2, [r4, #96]
	ldrh	r1, [r4, #120]
	.loc 2 1206 0
	ldr	ip, [r4, #3840]
	.loc 2 1204 0
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
.L843:
	.loc 2 1204 0 is_stmt 0 discriminator 1
	cmp	r3, r0
	ble	.L848
	.loc 2 1206 0 is_stmt 1
	ldr	r10, [r7, #4]
	lsl	lr, r3, #2
	ldr	r9, [ip, r3, lsl #2]
	cmp	r10, r9
	bls	.L844
	.loc 2 1208 0
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	.loc 2 1218 0
	uxth	ip, r3
	.loc 2 1208 0
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	cmp	r2, #0
	.loc 2 1216 0
	addeq	r1, r1, #1
	strheq	r1, [r8, #120]	@ movhi
	mov	r1, #0
.L846:
	.loc 2 1218 0 discriminator 1
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L847
	.loc 2 1223 0
	ldr	r1, [r7, #4]
	ldr	r2, [r5, #3840]
	str	r1, [r2, lr]
	.loc 2 1224 0
	lsl	r2, r3, #1
	ldr	r1, [r5, #124]
	strh	r6, [r1, r2]	@ movhi
.L848:
	.loc 2 1229 0
	cmp	r3, #0
	blt	.L826
	.loc 2 1237 0
	ldrh	r2, [r5, #96]
	ldrh	r1, [r5, #120]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L826
	.loc 2 1239 0
	add	r1, r1, #1
	.loc 2 1240 0
	ldr	r2, [r5, #3840]
	.loc 2 1239 0
	strh	r1, [r5, #120]	@ movhi
	.loc 2 1240 0
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	.loc 2 1241 0
	lsl	r3, r3, #1
	ldr	r2, [r5, #124]
	b	.L891
.L847:
	.loc 2 1220 0 discriminator 3
	ldr	r0, [r5, #3840]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	.loc 2 1221 0 discriminator 3
	lsl	r2, r2, #1
	ldr	r0, [r5, #124]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L846
.L844:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L843
.L827:
	.loc 2 1248 0
	ldr	r9, .L895+4
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	.loc 2 1250 0
	strheq	r6, [r9]	@ movhi
	.loc 2 1248 0
	beq	.L892
	.loc 2 1255 0
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L851
	.loc 2 1256 0
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.LVL702:
.L851:
	.loc 2 1257 0
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2564]
	cmp	r2, r3
	.loc 2 1265 0
	strhcs	r6, [r9, #4]	@ movhi
	.loc 2 1257 0
	bcs	.L826
	.loc 2 1259 0
	ldrh	r3, [r9]
	.loc 2 1260 0
	strh	r6, [r9]	@ movhi
	.loc 2 1259 0
	strh	r3, [r9, #4]	@ movhi
	.loc 2 1261 0
	ldr	r3, [r7, #4]
.L892:
	str	r3, [r4, #2564]
	b	.L826
.LVL703:
.L855:
	.loc 2 1292 0
	ldr	r1, [r5, #3828]
	ldrh	r3, [r1]
	cmp	r3, #0
	beq	.L857
.L860:
	.loc 2 1310 0
	ldr	r1, [r4, #124]
	ldrh	r2, [r1]
.LVL704:
	cmp	r2, #0
	beq	.L858
.L859:
	.loc 2 1328 0
	ldr	r3, [fp, #-60]
	ldrh	r2, [r3]
	ldr	r3, [r4, #104]
	cmp	r2, r3
	bls	.L890
	.loc 2 1328 0 is_stmt 0 discriminator 1
	mov	r2, #1328
	ldr	r1, .L895+12
	ldr	r0, .L895+16
	bl	printk
.LVL705:
.L890:
	.loc 2 1330 0 is_stmt 1
	mov	r0, #0
	sub	sp, fp, #40
.LCFI274:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL706:
.L857:
.LCFI275:
	.loc 2 1292 0 discriminator 1
	ldr	r2, .L895+20
.LVL707:
	ldrh	r2, [r2]
	cmp	r2, #0
	.loc 2 1294 0 discriminator 1
	ldrne	r0, [r5, #104]
	.loc 2 1292 0 discriminator 1
	beq	.L860
.L861:
	sxth	r2, r3
	.loc 2 1294 0 discriminator 1
	cmp	r2, r0
	bcs	.L860
	.loc 2 1296 0
	lsl	ip, r2, #1
	add	r3, r3, #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L861
	mov	r3, r2
	.loc 2 1302 0 discriminator 1
	mov	lr, #0
.L862:
	.loc 2 1298 0 discriminator 1
	ldr	r1, [r8, #104]
	cmp	r3, r1
	bcs	.L860
	.loc 2 1300 0 discriminator 3
	ldr	r0, [r8, #3828]
	lsl	r1, r3, #1
	sub	ip, r3, r2
	lsl	r5, ip, #1
	ldrh	r6, [r0, r1]
	strh	r6, [r0, r5]	@ movhi
	.loc 2 1301 0 discriminator 3
	ldr	r0, [r8, #3852]
	ldr	r5, [r0, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r5, [r0, ip, lsl #2]
	.loc 2 1302 0 discriminator 3
	ldr	r0, [r8, #3828]
	strh	lr, [r0, r1]	@ movhi
	b	.L862
.L858:
	.loc 2 1310 0 discriminator 1
	ldrh	r3, [r4, #120]
	cmp	r3, #0
	.loc 2 1312 0 discriminator 1
	ldrhne	r0, [r4, #96]
	.loc 2 1310 0 discriminator 1
	beq	.L859
.L867:
	sxth	r3, r2
	.loc 2 1312 0 discriminator 1
	cmp	r3, r0
	mov	ip, r3
	bge	.L859
	.loc 2 1314 0
	lsl	lr, r3, #1
	add	r2, r2, #1
	ldrh	lr, [r1, lr]
	cmp	lr, #0
	beq	.L867
	.loc 2 1320 0 discriminator 1
	mov	lr, #0
.L868:
	.loc 2 1316 0 discriminator 1
	ldrh	r2, [r8, #96]
	cmp	r3, r2
	bge	.L859
	.loc 2 1318 0 discriminator 3
	ldr	r1, [r8, #124]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r5, r0, #1
	ldrh	r6, [r1, r2]
	strh	r6, [r1, r5]	@ movhi
	.loc 2 1319 0 discriminator 3
	ldr	r1, [r8, #3840]
	ldr	r5, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r5, [r1, r0, lsl #2]
	.loc 2 1320 0 discriminator 3
	ldr	r1, [r8, #124]
	strh	lr, [r1, r2]	@ movhi
	b	.L868
.L896:
	.align	2
.L895:
	.word	.LANCHOR0
	.word	.LANCHOR0+2556
	.word	.LANCHOR0+36
	.word	.LANCHOR1+328
	.word	.LC1
	.word	.LANCHOR0+3904
.LFE210:
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
.LFB215:
	.loc 2 1466 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL708:
	.movsp ip
	mov	ip, sp
.LCFI276:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI277:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI278:
	.pad #28
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1466 0
	.loc 2 1470 0
	ldr	r3, [r0, #24]
.LVL709:
	.loc 2 1466 0
	mov	r4, r0
	.loc 2 1478 0
	ldr	r6, .L924
	.loc 2 1477 0
	mov	r1, #0
	.loc 2 1471 0
	ldr	r9, [r0, #12]
.LVL710:
	.loc 2 1483 0
	mov	r7, #0
	.loc 2 1470 0
	str	r3, [fp, #-48]
	.loc 2 1472 0
	ldr	r3, [r0, #16]
.LVL711:
	mov	r5, r6
	str	r3, [fp, #-60]
	.loc 2 1473 0
	ldrh	r3, [r0, #6]
.LVL712:
	str	r3, [fp, #-52]
	.loc 2 1474 0
	ldrh	r3, [r0, #8]
.LVL713:
	.loc 2 1477 0
	ldr	r0, [fp, #-48]
.LVL714:
	.loc 2 1474 0
	str	r3, [fp, #-56]
	.loc 2 1477 0
	ldr	r3, [fp, #-52]
.LVL715:
	lsl	r2, r3, #2
	bl	ftl_memset
.LVL716:
	.loc 2 1478 0
	ldr	r3, [r6, #2244]
	.loc 2 1479 0
	ldr	r8, [r6, #3244]
	.loc 2 1483 0
	str	r7, [r4, #32]
	.loc 2 1478 0
	str	r3, [r6, #3232]
	.loc 2 1481 0
	mvn	r3, #0
	.loc 2 1479 0
	str	r8, [r6, #3236]
	.loc 2 1481 0
	strh	r3, [r4]	@ movhi
	.loc 2 1482 0
	strh	r3, [r4, #2]	@ movhi
	.loc 2 1485 0
	mov	r3, #1
	.loc 2 1484 0
	str	r7, [r4, #28]
	.loc 2 1485 0
	str	r3, [r4, #36]
.LVL717:
.L898:
	.loc 2 1486 0 discriminator 1
	ldr	r2, [fp, #-56]
.LVL718:
	sxth	r3, r7
.LVL719:
	cmp	r3, r2
	bge	.L915
	.loc 2 1488 0
	ldr	r2, [fp, #-56]
	sub	r2, r2, #1
.LVL720:
	cmp	r3, r2
	lsl	r2, r3, #1
	bne	.L899
	.loc 2 1490 0
	ldrh	r0, [r9, r2]
.LVL721:
	mov	r1, #1
.LVL722:
	add	r10, r9, r2
.LVL723:
	str	r3, [fp, #-56]
	bl	FtlGetLastWrittenPage
.LVL724:
	.loc 2 1493 0
	ldr	r1, [fp, #-60]
.LVL725:
	.loc 2 1490 0
	sxth	r2, r0
.LVL726:
	.loc 2 1493 0
	ldr	r3, [fp, #-56]
.LVL727:
	.loc 2 1491 0
	add	r0, r0, #1
	.loc 2 1492 0
	strh	r7, [r4]	@ movhi
	.loc 2 1494 0
	mov	r9, #0
.LVL728:
	.loc 2 1491 0
	strh	r0, [r4, #2]	@ movhi
	.loc 2 1494 0
	add	r7, r2, #1
	.loc 2 1493 0
	ldr	r3, [r1, r3, lsl #2]
.LVL729:
	str	r3, [r4, #28]
	.loc 2 1497 0
	ldr	r3, .L924+4
.LVL730:
.L900:
	.loc 2 1494 0 discriminator 1
	sxth	r2, r9
.LVL731:
	cmp	r2, r7
	blt	.L902
.LVL732:
.L915:
	.loc 2 1547 0
	mov	r0, r4
.LVL733:
	bl	ftl_free_no_use_map_blk
.LVL734:
	.loc 2 1548 0
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #80]
	cmp	r2, r3
	bne	.L904
	.loc 2 1550 0
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.LVL735:
.L904:
	.loc 2 1553 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL736:
	.loc 2 1555 0
	mov	r0, r4
	bl	ftl_map_blk_gc
.LVL737:
	.loc 2 1557 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI279:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL738:
.L902:
.LCFI280:
	.loc 2 1496 0
	ldrh	r1, [r10]
.LVL739:
	.loc 2 1497 0
	mov	r0, r3
.LVL740:
	str	r3, [fp, #-56]
	.loc 2 1496 0
	orr	r2, r2, r1, lsl #10
	str	r2, [r6, #3228]
	.loc 2 1497 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL741:
	.loc 2 1500 0
	ldr	r2, [r6, #3224]
	ldr	r3, [fp, #-56]
	cmn	r2, #1
	beq	.L901
	.loc 2 1502 0
	ldrh	r2, [r8, #8]
	ldr	r1, [fp, #-52]
.LVL742:
	cmp	r1, r2
	bls	.L901
	.loc 2 1502 0 is_stmt 0 discriminator 1
	ldrh	r0, [r8]
	ldrh	r1, [r4, #4]
.LVL743:
	cmp	r0, r1
	.loc 2 1504 0 is_stmt 1 discriminator 1
	ldreq	r1, [r6, #3228]
	ldreq	r0, [fp, #-48]
.LVL744:
	streq	r1, [r0, r2, lsl #2]
.LVL745:
.L901:
	add	r9, r9, #1
	b	.L900
.LVL746:
.L899:
	.loc 2 1510 0
	ldr	r3, [r5, #2244]
.LVL747:
	.loc 2 1512 0
	ldr	r0, .L924+4
.LVL748:
	.loc 2 1510 0
	str	r3, [r5, #3232]
	.loc 2 1511 0
	add	r3, r9, r2
	str	r3, [fp, #-64]
	ldrh	r2, [r9, r2]
	ldrh	r3, [r5, #80]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	.loc 2 1512 0
	mov	r2, #1
	mov	r1, r2
.LVL749:
	.loc 2 1511 0
	str	r3, [r5, #3228]
	.loc 2 1512 0
	bl	FlashReadPages
.LVL750:
	.loc 2 1515 0
	ldr	r3, [r5, #3224]
	cmn	r3, #1
	beq	.L917
	.loc 2 1515 0 is_stmt 0 discriminator 1
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L917
	.loc 2 1515 0 discriminator 2
	ldrh	r2, [r8, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L906
.L917:
	mov	r10, #0
.LVL751:
.L907:
	.loc 2 1530 0 is_stmt 1 discriminator 1
	ldrh	r2, [r5, #80]
.LVL752:
	sxth	r3, r10
	cmp	r3, r2
	bge	.L913
	.loc 2 1532 0
	ldr	r2, [fp, #-64]
	.loc 2 1533 0
	ldr	r0, .L924+4
	.loc 2 1532 0
	ldrh	r2, [r2]
	orr	r3, r3, r2, lsl #10
	.loc 2 1533 0
	mov	r2, #1
	mov	r1, r2
.LVL753:
	.loc 2 1532 0
	str	r3, [r5, #3228]
	.loc 2 1533 0
	bl	FlashReadPages
.LVL754:
	.loc 2 1536 0
	ldr	r3, [r5, #3224]
	cmn	r3, #1
	beq	.L911
	.loc 2 1538 0
	ldrh	r3, [r8, #8]
	ldr	r2, [fp, #-52]
.LVL755:
	cmp	r2, r3
	bls	.L911
	.loc 2 1538 0 is_stmt 0 discriminator 1
	ldrh	r1, [r8]
	ldrh	r2, [r4, #4]
.LVL756:
	cmp	r1, r2
	.loc 2 1540 0 is_stmt 1 discriminator 1
	ldreq	r2, [r5, #3228]
	ldreq	r1, [fp, #-48]
.LVL757:
	streq	r2, [r1, r3, lsl #2]
.LVL758:
.L911:
	add	r10, r10, #1
	b	.L907
.LVL759:
.L906:
	mov	r1, #0
	.loc 2 1523 0 discriminator 1
	mov	ip, #4
.LVL760:
.L908:
	.loc 2 1518 0 discriminator 1
	ldrh	r2, [r5, #80]
	sxth	r3, r1
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L910
.L913:
	add	r7, r7, #1
	b	.L898
.L910:
	.loc 2 1520 0
	ldr	r0, [r5, #2244]
.LVL761:
	add	r1, r1, #1
	.loc 2 1521 0
	ldr	r10, [fp, #-52]
	.loc 2 1520 0
	ldr	r2, [r0, r3, lsl #3]
	.loc 2 1521 0
	uxth	lr, r2
	cmp	r10, lr
	.loc 2 1523 0
	addhi	r3, ip, r3, lsl #3
	movhi	r2, lr
	ldrhi	r3, [r0, r3]
	ldrhi	r0, [fp, #-48]
.LVL762:
	strhi	r3, [r0, r2, lsl #2]
	b	.L908
.L925:
	.align	2
.L924:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
.LFE215:
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
.LFB216:
	.loc 2 1560 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI281:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI282:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI283:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1560 0
	.loc 2 1561 0
	ldr	r3, .L927
	ldrh	r2, [r3, #96]
	add	r0, r3, #3904
	add	r0, r0, #4
	strh	r2, [r0, #10]	@ movhi
	.loc 2 1562 0
	ldr	r2, .L927+4
	strh	r2, [r0, #4]	@ movhi
	.loc 2 1563 0
	ldrh	r2, [r3, #120]
	strh	r2, [r0, #8]	@ movhi
	.loc 2 1564 0
	ldrh	r2, [r3, #98]
	strh	r2, [r0, #6]	@ movhi
	.loc 2 1565 0
	ldr	r2, [r3, #124]
	str	r2, [r3, #3920]
	.loc 2 1566 0
	ldr	r2, [r3, #3840]
	str	r2, [r3, #3924]
	.loc 2 1567 0
	ldr	r2, [r3, #3836]
	str	r2, [r3, #3928]
	.loc 2 1568 0
	ldr	r2, [r3, #3844]
	str	r2, [r3, #3932]
	.loc 2 1570 0
	bl	FtlMapTblRecovery
.LVL763:
	.loc 2 1573 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L928:
	.align	2
.L927:
	.word	.LANCHOR0
	.word	-3962
.LFE216:
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlL2PDataInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
.LFB217:
	.loc 2 1576 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI284:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI285:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI286:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1576 0
	.loc 2 1578 0
	mov	r1, #0
	.loc 2 1583 0
	mvn	r5, #0
	.loc 2 1578 0
	ldr	r4, .L932
	ldr	r2, [r4, #104]
	ldr	r0, [r4, #3832]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL764:
	.loc 2 1579 0
	ldrh	r3, [r4, #86]
	mov	r1, #255
	ldrh	r2, [r4, #114]
	ldr	r0, [r4, #3856]
	mul	r2, r2, r3
	bl	ftl_memset
.LVL765:
	.loc 2 1580 0
	mov	r2, #0
	mov	r3, r4
	.loc 2 1582 0
	mov	lr, #12
	mov	r4, r2
.L930:
	.loc 2 1580 0 discriminator 1
	ldrh	r0, [r3, #114]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r0, r1
	bhi	.L931
	.loc 2 1586 0
	ldr	r2, .L932+4
	mvn	r1, #0
	.loc 2 1587 0
	movw	r0, #3864
	strh	r1, [r3, r0]	@ movhi
	.loc 2 1586 0
	strh	r1, [r2, #2]	@ movhi
	.loc 2 1588 0
	ldr	r1, [r3, #104]
	strh	r1, [r2, #10]	@ movhi
	.loc 2 1589 0
	ldr	r1, .L932+8
	strh	r1, [r2, #4]	@ movhi
	.loc 2 1590 0
	ldrh	r1, [r2, #40]
	strh	r1, [r2, #8]	@ movhi
	.loc 2 1591 0
	ldrh	r1, [r3, #112]
	strh	r1, [r2, #6]	@ movhi
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
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L931:
	.loc 2 1582 0 discriminator 3
	uxth	r2, r2
	ldr	r1, [r3, #2424]
	mul	r0, lr, r2
	add	r6, r1, r0
	str	r4, [r6, #4]
	.loc 2 1583 0 discriminator 3
	strh	r5, [r1, r0]	@ movhi
	.loc 2 1584 0 discriminator 3
	ldr	r1, [r3, #2424]
	add	r1, r1, r0
	ldrh	r0, [r3, #86]
	mul	r2, r2, r0
	ldr	r0, [r3, #3856]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L930
.L933:
	.align	2
.L932:
	.word	.LANCHOR0
	.word	.LANCHOR0+3864
	.word	-3902
.LFE217:
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlLoadMapInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
.LFB218:
	.loc 2 1599 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI287:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI288:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI289:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1599 0
	.loc 2 1600 0
	bl	FtlL2PDataInit
.LVL766:
	.loc 2 1602 0
	ldr	r0, .L935
	bl	FtlMapTblRecovery
.LVL767:
	.loc 2 1605 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L936:
	.align	2
.L935:
	.word	.LANCHOR0+3864
.LFE218:
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVariablesInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
.LFB147:
	.loc 3 493 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI290:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI291:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI292:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 493 0
	.loc 3 497 0
	mvn	r3, #0
	.loc 3 496 0
	mov	r5, #0
	ldr	r4, .L938
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
.LVL768:
	.loc 3 503 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #2248]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL769:
	.loc 3 504 0
	ldrh	r2, [r4, #18]
	mov	r1, r5
	ldr	r0, [r4, #3816]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL770:
.LBB409:
.LBB410:
.LBB411:
	.loc 1 202 0
	add	r0, r4, #2496
	mov	r1, #48
	add	r0, r0, #8
	bl	__memzero
.LVL771:
.LBE411:
.LBE410:
.LBE409:
.LBB412:
.LBB413:
.LBB414:
	add	r0, r4, #2608
	mov	r1, #512
	add	r0, r0, #8
	bl	__memzero
.LVL772:
.LBE414:
.LBE413:
.LBE412:
	.loc 3 507 0
	bl	FtlGcBufInit
.LVL773:
	.loc 3 508 0
	bl	FtlL2PDataInit
.LVL774:
	.loc 3 510 0
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L939:
	.align	2
.L938:
	.word	.LANCHOR0
.LFE147:
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
.LFB225:
	.loc 2 2067 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI293:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI294:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI295:
	.pad #20
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2067 0
	.loc 2 2071 0
	ldr	r4, .L951
	mov	r2, #6
	.loc 2 2072 0
	mov	r5, #0
	.loc 2 2071 0
	mov	r1, #0
	.loc 2 2077 0
	mov	r9, r5
.LVL775:
	mov	r7, r5
.LVL776:
	.loc 2 2071 0
	ldrh	r3, [r4, #18]
	.loc 2 2082 0
	add	r10, r4, #36
	.loc 2 2071 0
	ldr	r0, [r4, #2252]
	mul	r2, r2, r3
	bl	ftl_memset
.LVL777:
	.loc 2 2075 0
	movw	r3, #2268
	.loc 2 2072 0
	str	r5, [r4, #2272]
	.loc 2 2075 0
	strh	r5, [r4, r3]	@ movhi
	.loc 2 2076 0
	movw	r3, #2276
	.loc 2 2073 0
	str	r5, [r4, #2256]
	.loc 2 2074 0
	str	r5, [r4, #2264]
	.loc 2 2076 0
	strh	r5, [r4, r3]	@ movhi
.LVL778:
.L941:
	.loc 2 2079 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L947
	.loc 2 2084 0
	ldrh	r3, [r4, #78]
	.loc 2 2081 0
	ldrh	r2, [r4, #8]
	.loc 2 2084 0
	str	r3, [fp, #-48]
	mov	r3, #0
	mov	r6, r3
	b	.L948
.L943:
	.loc 2 2082 0
	mov	r1, r8
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	str	r2, [fp, #-56]
	str	r3, [fp, #-52]
	bl	V2P_block
.LVL779:
	.loc 2 2083 0
	bl	FtlBbmIsBadBlock
.LVL780:
	cmp	r0, #0
	ldr	r3, [fp, #-52]
	.loc 2 2084 0
	ldreq	r1, [fp, #-48]
	.loc 2 2083 0
	ldr	r2, [fp, #-56]
	add	r3, r3, #1
	.loc 2 2084 0
	addeq	r6, r6, r1
	uxtheq	r6, r6
.L948:
	.loc 2 2081 0 discriminator 1
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L943
	.loc 2 2086 0
	cmp	r6, #0
	moveq	r0, r6
	beq	.L944
	.loc 2 2087 0
	mov	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
.LVL781:
.L944:
	.loc 2 2088 0
	ldr	r1, [r4, #2252]
	uxth	r3, r5
	mov	r2, #6
	mla	r2, r2, r3, r1
	strh	r0, [r2, #4]	@ movhi
	.loc 2 2090 0
	ldr	r2, .L951+4
	ldrh	r1, [r2]
	cmp	r1, r8
	beq	.L945
	.loc 2 2090 0 is_stmt 0 discriminator 1
	ldrh	r1, [r2, #48]
	cmp	r1, r8
	beq	.L945
	.loc 2 2091 0 is_stmt 1
	ldrh	r2, [r2, #96]
	cmp	r2, r8
	beq	.L945
	.loc 2 2094 0
	ldr	r2, [r4, #2260]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L946
	.loc 2 2095 0
	add	r9, r9, #1
.LVL782:
	.loc 2 2096 0
	mov	r0, r8
	.loc 2 2095 0
	uxth	r9, r9
.LVL783:
	.loc 2 2096 0
	bl	INSERT_FREE_LIST
.LVL784:
.L945:
	add	r5, r5, #1
	b	.L941
.L946:
	.loc 2 2098 0
	add	r7, r7, #1
.LVL785:
	.loc 2 2099 0
	mov	r0, r8
	.loc 2 2098 0
	uxth	r7, r7
.LVL786:
	.loc 2 2099 0
	bl	INSERT_DATA_LIST
.LVL787:
	b	.L945
.L947:
	.loc 2 2103 0
	movw	r2, #2268
	strh	r7, [r4, r2]	@ movhi
	.loc 2 2105 0
	add	r7, r7, r9
.LVL788:
	cmp	r7, r3
	.loc 2 2104 0
	movw	r2, #2276
	strh	r9, [r4, r2]	@ movhi
	.loc 2 2105 0
	ble	.L949
	.loc 2 2105 0 is_stmt 0 discriminator 1
	movw	r2, #2105
	ldr	r1, .L951+8
	ldr	r0, .L951+12
	bl	printk
.LVL789:
.L949:
	.loc 2 2107 0 is_stmt 1
	mov	r0, #0
	sub	sp, fp, #40
.LCFI296:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL790:
.L952:
	.align	2
.L951:
	.word	.LANCHOR0
	.word	.LANCHOR0+2280
	.word	.LANCHOR1+342
	.word	.LC1
.LFE225:
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
.LFB249:
	.loc 5 202 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI297:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI298:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI299:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 202 0
	.loc 5 203 0
	mov	r3, #0
	movw	r2, #3172
	ldr	r4, .L954
	.loc 5 205 0
	mov	r1, #255
	.loc 5 203 0
	strh	r3, [r4, r2]	@ movhi
	.loc 5 204 0
	movw	r2, #3180
	strh	r3, [r4, r2]	@ movhi
	.loc 5 205 0
	ldrh	r2, [r4, #82]
	ldr	r0, [r4, #3168]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL791:
	.loc 5 206 0
	ldrh	r3, [r4, #82]
	mov	r2, #12
	mov	r1, #255
	ldr	r0, [r4, #3176]
	mul	r2, r2, r3
	bl	ftl_memset
.LVL792:
	.loc 5 207 0
	bl	FtlGcBufInit
.LVL793:
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L955:
	.align	2
.L954:
	.word	.LANCHOR0
.LFE249:
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
.LFB250:
	.loc 5 211 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL794:
	.movsp ip
	mov	ip, sp
.LCFI300:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI301:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI302:
	.pad #28
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 211 0
	.loc 5 220 0
	ldr	r3, .L979
	.loc 5 211 0
	mov	r6, r0
	str	r1, [fp, #-48]
	.loc 5 220 0
	ldrh	r4, [r3, #4]
.LVL795:
	.loc 5 221 0
	movw	r3, #65535
	cmp	r4, r3
	beq	.L973
	.loc 5 223 0
	cmp	r4, #0
	bne	.L957
.LVL796:
.L958:
	.loc 5 224 0
	bl	FtlGcPageVarInit
.LVL797:
	b	.L959
.LVL798:
.L973:
	.loc 5 222 0
	mov	r4, #0
.L957:
	.loc 5 223 0 discriminator 1
	ldr	r3, .L979+4
	ldr	r2, [fp, #-48]
.LVL799:
	ldrh	r3, [r3, #78]
	cmp	r3, r2
	beq	.L958
.L959:
	.loc 5 231 0
	ldr	r5, .L979+4
	.loc 5 222 0
	mov	r8, #0
.LVL800:
.L967:
	.loc 5 228 0
	ldrh	r2, [r6]
.LVL801:
	movw	r3, #65535
	.loc 5 226 0
	mov	r0, #0
	strb	r0, [r6, #8]
	.loc 5 228 0
	cmp	r2, r3
	beq	.L960
.L970:
	.loc 5 231 0
	ldrh	r3, [r5, #8]
	mov	r2, #0
	.loc 5 234 0
	ldr	r0, [r5, #3792]
	add	ip, r6, #16
	.loc 5 236 0
	ldrh	r10, [r5, #88]
	.loc 5 230 0
	mov	r7, r2
	.loc 5 231 0
	str	r3, [fp, #-52]
	.loc 5 233 0
	movw	r9, #65535
	.loc 5 235 0
	ldr	r3, [r5, #3152]
	str	r3, [fp, #-56]
	ldrh	r3, [r5, #86]
	str	r3, [fp, #-60]
	.loc 5 236 0
	ldr	r3, [r5, #3156]
	str	r3, [fp, #-64]
.LVL802:
.L961:
	.loc 5 231 0 discriminator 1
	ldr	r1, [fp, #-52]
	uxth	r3, r2
.LVL803:
	cmp	r1, r3
	bhi	.L963
	.loc 5 241 0
	mov	r9, #0
	.loc 5 240 0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.LVL804:
.L964:
	.loc 5 241 0 discriminator 1
	uxth	r3, r9
	cmp	r7, r3
	bhi	.L968
	.loc 5 268 0
	ldr	r3, [fp, #-48]
.LVL805:
	.loc 5 266 0
	add	r8, r8, #1
	.loc 5 264 0
	add	r4, r4, #1
.LVL806:
	uxth	r4, r4
.LVL807:
	.loc 5 268 0
	cmp	r3, r8
	bls	.L969
.L971:
	.loc 5 274 0
	ldrh	r3, [r5, #78]
.LVL808:
	cmp	r3, r4
	bhi	.L970
	.loc 5 265 0
	mov	r0, #0
.LVL809:
	b	.L960
.LVL810:
.L963:
	.loc 5 232 0
	ldrh	r3, [ip], #2
.LVL811:
	.loc 5 233 0
	cmp	r3, r9
	beq	.L962
	.loc 5 234 0
	mov	r1, #20
	orr	r3, r4, r3, lsl #10
.LVL812:
	mla	r1, r1, r7, r0
	str	r3, [r1, #4]
	.loc 5 235 0
	ldr	r3, [fp, #-60]
	mul	r3, r3, r7
	add	lr, r3, #3
	cmp	r3, #0
	movlt	r3, lr
	ldr	lr, [fp, #-56]
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #8]
	.loc 5 236 0
	mul	r3, r10, r7
	.loc 5 237 0
	add	r7, r7, #1
.LVL813:
	uxth	r7, r7
.LVL814:
	.loc 5 236 0
	add	lr, r3, #3
	cmp	r3, #0
	movlt	r3, lr
	ldr	lr, [fp, #-64]
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.LVL815:
.L962:
	add	r2, r2, #1
	b	.L961
.LVL816:
.L968:
	.loc 5 242 0
	mov	r3, #20
	ldr	r2, [r5, #3792]
	mul	r10, r3, r9
	str	r2, [fp, #-60]
	add	r3, r2, r10
	ldr	r1, [r3, #4]
.LVL817:
	str	r3, [fp, #-56]
	.loc 5 243 0
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-52]
	bl	P2V_plane
.LVL818:
	.loc 5 245 0
	ldr	r2, [fp, #-60]
	ldr	r2, [r2, r10]
	cmp	r2, #0
	bne	.L965
	.loc 5 247 0
	ldr	r3, [fp, #-56]
	.loc 5 248 0
	movw	r2, #65535
	add	r9, r9, #1
	ldr	r1, [fp, #-52]
.LVL819:
	.loc 5 247 0
	ldr	r3, [r3, #12]
.LVL820:
	.loc 5 248 0
	ldrh	ip, [r3]
	cmp	ip, r2
	bne	.L966
	.loc 5 249 0
	mov	r3, #1
.LVL821:
	str	r3, [r5, #3960]
.LVL822:
.L960:
	.loc 5 281 0
	ldr	r3, .L979
	mvn	r2, #0
	.loc 5 283 0
	strb	r0, [r6, #6]
	.loc 5 284 0
	mov	r1, r4
.LVL823:
	.loc 5 282 0
	strh	r4, [r6, #2]	@ movhi
	.loc 5 281 0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 284 0
	mov	r2, r0
	mov	r0, r6
.LVL824:
	bl	ftl_sb_update_avl_pages
.LVL825:
	.loc 5 285 0
	b	.L972
.LVL826:
.L966:
	.loc 5 252 0
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL827:
	b	.L964
.L965:
	.loc 5 256 0
	ldrh	r3, [r6]
	mov	r4, #0
.LVL828:
	ldr	r2, [r5, #2260]
	lsl	r3, r3, #1
	strh	r4, [r2, r3]	@ movhi
	.loc 5 257 0
	ldrh	r0, [r6]
	bl	INSERT_FREE_LIST
.LVL829:
	.loc 5 258 0
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
	.loc 5 260 0
	bl	FtlGcPageVarInit
.LVL830:
	.loc 5 261 0
	b	.L967
.LVL831:
.L969:
	.loc 5 268 0 discriminator 1
	ldr	r2, .L979
	movw	r1, #65535
	ldrh	r3, [r2, #4]
.LVL832:
	cmp	r3, r1
	beq	.L971
	.loc 5 269 0
	add	r3, r3, r8
	strh	r3, [r2, #4]	@ movhi
	.loc 5 270 0
	ldrh	r3, [r5, #78]
	cmp	r3, r4
	bls	.L971
.L972:
	.loc 5 286 0
	mvn	r0, #0
	sub	sp, fp, #40
.LCFI303:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL833:
.L980:
	.align	2
.L979:
	.word	.LANCHOR2
	.word	.LANCHOR0
.LFE250:
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	ftl_memcpy
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcpy, %function
ftl_memcpy:
	.fnstart
.LFB268:
	.loc 1 206 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL834:
	.movsp ip
	mov	ip, sp
.LCFI304:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI305:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI306:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 206 0
	.loc 1 207 0
	bl	memcpy
.LVL835:
	.loc 1 208 0
	ldmfd	sp, {fp, sp, pc}
.LFE268:
	.fnend
	.size	ftl_memcpy, .-ftl_memcpy
	.align	2
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
.LFB166:
	.loc 4 80 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI307:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI308:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI309:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 80 0
	.loc 4 87 0
	ldr	r4, .L991
	.loc 4 91 0
	mov	r5, #0
.LVL836:
	.loc 4 92 0
	ldr	r7, .L991+4
	.loc 4 89 0
	mov	r1, #0
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
.LVL837:
.L983:
	.loc 4 91 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	blt	.L984
	.loc 4 94 0
	ldr	r6, [r4, #3236]
.LVL838:
.LBB420:
.LBB421:
.LBB422:
	.loc 1 202 0
	mov	r2, #16
	mov	r1, #255
.LBE422:
.LBE421:
.LBE420:
	.loc 4 108 0
	ldr	r9, .L991+8
	.loc 4 109 0
	ldr	r8, .L991+12
	.loc 4 84 0
	mov	r5, #0
.LVL839:
.LBB427:
.LBB425:
.LBB423:
	.loc 1 202 0
	mov	r0, r6
.LBE423:
.LBE425:
.LBE427:
	.loc 4 82 0
	mov	r7, r5
.LVL840:
.LBB428:
.LBB426:
.LBB424:
	.loc 1 202 0
	bl	memset
.LVL841:
.LBE424:
.LBE426:
.LBE428:
	.loc 4 97 0
	ldr	r3, .L991+16
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
.L985:
	.loc 4 104 0
	ldr	r3, [r4, #2244]
	.loc 4 106 0
	mov	r10, #0
	.loc 4 107 0
	ldrh	r1, [r4, #128]
	ldrh	r2, [r4, #130]
	.loc 4 104 0
	str	r3, [r4, #3232]
	.loc 4 105 0
	ldr	r3, [r4, #3244]
	.loc 4 106 0
	str	r10, [r4, #3224]
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
.LVL842:
	.loc 4 109 0
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL843:
	.loc 4 110 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r4, #130]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L986
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
	str	r3, [r4, #3228]
	.loc 4 121 0
	str	r3, [r0, #4]
	.loc 4 122 0
	bl	FlashEraseBlocks
.LVL844:
	.loc 4 123 0
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL845:
.L986:
	.loc 4 126 0
	ldrh	r3, [r4, #130]
	add	r3, r3, #1
	strh	r3, [r4, #130]	@ movhi
	.loc 4 127 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	bne	.L987
	.loc 4 130 0
	add	r5, r5, #1
.LVL846:
	.loc 4 129 0
	ldr	r1, [r4, #3228]
	.loc 4 130 0
	uxth	r5, r5
.LVL847:
	.loc 4 129 0
	ldr	r0, .L991+20
	bl	printk
.LVL848:
	.loc 4 131 0
	cmp	r5, #3
	bls	.L985
	.loc 4 132 0
	mov	r2, r5
	ldr	r1, [r4, #3228]
	ldr	r0, .L991+24
	bl	printk
.LVL849:
.L989:
	b	.L989
.LVL850:
.L984:
	.loc 4 92 0 discriminator 3
	ldrh	r2, [r7]
	ldr	r3, [r4, #3232]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	.loc 4 91 0 discriminator 3
	add	r5, r5, #1
	.loc 4 92 0 discriminator 3
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
.LVL851:
	b	.L983
.LVL852:
.L990:
	mov	r7, #1
	b	.L985
.L987:
	.loc 4 139 0
	cmp	r7, #0
	beq	.L990
	.loc 4 146 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI310:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL853:
.L992:
	.align	2
.L991:
	.word	.LANCHOR0
	.word	.LANCHOR0+3860
	.word	.LC90
	.word	.LANCHOR0+3224
	.word	-3887
	.word	.LC91
	.word	.LC92
.LFE166:
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
.LFB238:
	.loc 2 2531 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL854:
	.movsp ip
	mov	ip, sp
.LCFI311:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI312:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI313:
	.pad #20
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2531 0
	.loc 2 2537 0
	ldr	r9, .L1028
	.loc 2 2531 0
	mov	r5, r0
	mov	r4, r9
.LVL855:
.L994:
	.loc 2 2537 0
	ldr	r7, .L1028+4
.LVL856:
	ldrh	r2, [r7, #-8]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r9, #16]
	cmp	r3, r2
	ble	.L995
	.loc 2 2537 0 is_stmt 0 discriminator 1
	movw	r2, #2537
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL857:
.L995:
	.loc 2 2539 0 is_stmt 1
	ldr	r3, .L1028+16
	cmp	r5, r3
	movne	r1, #0
.LVL858:
	bne	.L996
	.loc 2 2540 0
	ldrh	r3, [r7]
	ldr	r1, [r4, #3140]
.LVL859:
	mul	r2, r1, r3
	lsr	r1, r3, #1
	add	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
.LVL860:
	.loc 2 2544 0
	cmp	r1, #0
	.loc 2 2545 0
	subne	r1, r1, #1
.LVL861:
	uxthne	r1, r1
.LVL862:
.L996:
	.loc 2 2546 0
	ldr	r0, .L1028+20
	bl	List_pop_index_node
.LVL863:
	ldrh	r3, [r7]
	mov	r6, r0
	uxth	r8, r0
	cmp	r3, #0
	bne	.L997
	.loc 2 2546 0 is_stmt 0 discriminator 1
	movw	r2, #2546
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL864:
.L997:
	.loc 2 2546 0 discriminator 3
	ldrh	r3, [r7]
	.loc 2 2550 0 is_stmt 1 discriminator 3
	mov	r0, r5
	.loc 2 2546 0 discriminator 3
	sub	r3, r3, #1
	strh	r3, [r7]	@ movhi
	.loc 2 2549 0 discriminator 3
	strh	r8, [r5]	@ movhi
	.loc 2 2550 0 discriminator 3
	bl	make_superblock
.LVL865:
	.loc 2 2551 0 discriminator 3
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L998
	.loc 2 2555 0
	uxth	r6, r6
	ldr	r3, [r4, #2260]
	mvn	r2, #0
	lsl	r6, r6, #1
	.loc 2 2556 0
	mov	r0, r8
	.loc 2 2555 0
	strh	r2, [r3, r6]	@ movhi
	.loc 2 2556 0
	bl	INSERT_DATA_LIST
.LVL866:
	.loc 2 2557 0
	ldrh	r2, [r7, #-8]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L994
	.loc 2 2557 0 is_stmt 0 discriminator 1
	movw	r2, #2557
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL867:
	b	.L994
.L998:
	.loc 2 2560 0 is_stmt 1
	ldrh	r2, [r7, #-8]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1000
	.loc 2 2560 0 is_stmt 0 discriminator 1
	mov	r2, #2560
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL868:
.L1000:
	.loc 2 2564 0 is_stmt 1 discriminator 1
	ldr	r0, [r4, #3760]
	mov	r2, #20
	ldrh	r1, [r4, #8]
	mov	r7, #0
.LVL869:
	add	ip, r5, #16
	mov	lr, r7
	mov	r3, r0
	mla	r1, r2, r1, r0
	str	r1, [fp, #-48]
.L1001:
	.loc 2 2563 0 discriminator 1
	ldr	r1, [fp, #-48]
	cmp	r1, r3
	bne	.L1003
	.loc 2 2571 0
	cmp	r7, #0
	bne	.L1004
	.loc 2 2571 0 is_stmt 0 discriminator 1
	movw	r2, #2571
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL870:
.L1004:
	.loc 2 2573 0 is_stmt 1
	ldr	r3, .L1028+24
	ldrh	r3, [r3]
	cmp	r3, r8
	bne	.L1005
	.loc 2 2573 0 is_stmt 0 discriminator 1
	movw	r2, #2573
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL871:
.L1005:
	.loc 2 2574 0 is_stmt 1
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, #0
	bne	.L1006
	.loc 2 2575 0
	ldr	r1, [r4, #2248]
	lsl	r2, r6, #1
	ldrh	r3, [r1, r2]
	cmp	r3, #0
	.loc 2 2576 0
	ldrhne	r0, [r4, #68]
	.loc 2 2578 0
	moveq	r3, #2
	.loc 2 2576 0
	addne	r3, r3, r0
	.loc 2 2580 0
	mov	r0, r8
	.loc 2 2578 0
	strh	r3, [r1, r2]	@ movhi
	.loc 2 2580 0
	mov	r1, #0
	.loc 2 2579 0
	ldr	r3, [r4, #2480]
	add	r3, r3, #1
	str	r3, [r4, #2480]
	.loc 2 2580 0
	bl	ftl_set_blk_mode
.LVL872:
.L1009:
	.loc 2 2587 0
	ldr	r3, [r4, #2248]
	lsl	r6, r6, #1
	ldr	r2, [r4, #2496]
	.loc 2 2590 0
	ldr	r0, [r4, #2480]
	.loc 2 2587 0
	ldrh	r3, [r3, r6]
	.loc 2 2590 0
	ldrh	r1, [r4, #16]
	.loc 2 2587 0
	cmp	r3, r2
	.loc 2 2590 0
	ldrh	r2, [r4, #68]
	.loc 2 2588 0
	strhi	r3, [r4, #2496]
	.loc 2 2590 0
	ldr	r3, [r4, #2484]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
.LVL873:
	.loc 2 2591 0
	ldr	r2, [r4, #3820]
	.loc 2 2594 0
	ldr	r1, [r4, #3760]
	.loc 2 2590 0
	str	r0, [r4, #2488]
	.loc 2 2591 0
	ldr	r3, [r2, #16]
	.loc 2 2594 0
	ldr	r0, .L1028+28
	.loc 2 2591 0
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #20
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #24
.L1011:
	add	r3, r3, #20
	.loc 2 2593 0 discriminator 1
	cmp	r2, r3
	bne	.L1012
	.loc 2 2595 0
	mov	r2, r7
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	ldr	r0, [r4, #3760]
	.loc 2 2597 0
	mov	r10, #0
	.loc 2 2595 0
	bl	FlashEraseBlocks
.LVL874:
	.loc 2 2596 0
	mov	r2, r10
.LVL875:
	.loc 2 2598 0
	mov	r1, #20
.L1013:
	.loc 2 2597 0 discriminator 1
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1015
	.loc 2 2608 0
	cmp	r2, #0
	ble	.L1016
	.loc 2 2609 0
	mov	r0, r8
	bl	update_multiplier_value
.LVL876:
	.loc 2 2610 0
	bl	FtlBbmTblFlush
.LVL877:
.L1016:
	.loc 2 2613 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
.LVL878:
	cmp	r2, #0
	bne	.L1017
	.loc 2 2615 0
	ldr	r3, [r4, #2260]
	mvn	r2, #0
	.loc 2 2616 0
	mov	r0, r8
	.loc 2 2615 0
	strh	r2, [r3, r6]	@ movhi
	.loc 2 2616 0
	bl	INSERT_DATA_LIST
.LVL879:
	.loc 2 2617 0
	b	.L994
.L1003:
	.loc 2 2564 0
	str	lr, [r3, #8]
	.loc 2 2566 0
	movw	r10, #65535
	.loc 2 2565 0
	str	lr, [r3, #12]
	add	r3, r3, #20
	.loc 2 2566 0
	ldrh	r1, [ip], #2
	cmp	r1, r10
	.loc 2 2567 0
	mlane	r10, r2, r7, r0
	lslne	r1, r1, #10
	.loc 2 2568 0
	addne	r7, r7, #1
.LVL880:
	uxthne	r7, r7
.LVL881:
	.loc 2 2567 0
	strne	r1, [r10, #4]
	b	.L1001
.L1006:
	.loc 2 2582 0
	ldr	r1, [r4, #2248]
	lsl	r3, r6, #1
.LBB429:
.LBB430:
	mov	r0, r8
.LBE430:
.LBE429:
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	.loc 2 2583 0
	ldr	r3, [r4, #2484]
	add	r3, r3, #1
	str	r3, [r4, #2484]
.LBB432:
.LBB431:
	bl	ftl_set_blk_mode.part.6
.LVL882:
	b	.L1009
.L1012:
.LBE431:
.LBE432:
	.loc 2 2594 0 discriminator 3
	ldr	r1, [r3, #-20]
	and	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1011
.LVL883:
.L1015:
	.loc 2 2598 0
	mul	r3, r1, r10
	ldr	r0, [r4, #3760]
	add	ip, r0, r3
	ldr	r3, [r0, r3]
	cmn	r3, #1
	bne	.L1014
	.loc 2 2600 0
	ldr	r0, [ip, #4]
	.loc 2 2599 0
	add	r2, r2, #1
	str	r1, [fp, #-56]
	str	r3, [fp, #-52]
	.loc 2 2601 0
	ubfx	r0, r0, #10, #16
	.loc 2 2599 0
	str	r2, [fp, #-48]
	.loc 2 2601 0
	bl	FtlBbmMapBadBlock
.LVL884:
	.loc 2 2602 0
	ldr	r3, [fp, #-52]
	add	r0, r5, r10, lsl #1
	.loc 2 2603 0
	ldr	r1, [fp, #-56]
	ldr	r2, [fp, #-48]
.LVL885:
	.loc 2 2602 0
	strh	r3, [r0, #16]	@ movhi
	.loc 2 2603 0
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r5, #7]
.L1014:
	add	r10, r10, #1
	b	.L1013
.LVL886:
.L1017:
	.loc 2 2620 0
	ldrh	r3, [r4, #78]
	.loc 2 2621 0
	strh	r8, [r5]	@ movhi
	.loc 2 2620 0
	smulbb	r3, r3, r2
	.loc 2 2622 0
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	.loc 2 2623 0
	strb	r2, [r5, #6]
	.loc 2 2624 0
	ldr	r2, [r4, #2472]
	.loc 2 2620 0
	uxth	r3, r3
	.loc 2 2625 0
	ldr	r1, [r4, #2260]
	.loc 2 2620 0
	strh	r3, [r5, #4]	@ movhi
	.loc 2 2624 0
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #2472]
	.loc 2 2625 0
	ldrh	r2, [r5]
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
	.loc 2 2626 0
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1018
	.loc 2 2626 0 is_stmt 0 discriminator 2
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1019
.L1018:
	.loc 2 2626 0 discriminator 3
	movw	r2, #2626
	ldr	r1, .L1028+8
	ldr	r0, .L1028+12
	bl	printk
.LVL887:
.L1019:
	.loc 2 2628 0 is_stmt 1
	mov	r0, #0
	sub	sp, fp, #40
.LCFI314:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL888:
.L1029:
	.align	2
.L1028:
	.word	.LANCHOR0
	.word	.LANCHOR0+2276
	.word	.LANCHOR1+360
	.word	.LC1
	.word	.LANCHOR0+2376
	.word	.LANCHOR0+2272
	.word	.LANCHOR0+2568
	.word	-1024
.LFE238:
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
.LFB257:
	.loc 5 390 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL889:
	.movsp ip
	mov	ip, sp
.LCFI315:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI316:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI317:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 390 0
	.loc 5 395 0
	ldr	r6, .L1042
	movw	r3, #3186
	.loc 5 390 0
	str	r0, [fp, #-48]
	.loc 5 395 0
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
.LBB435:
.LBB436:
	.loc 5 398 0
	ldrne	r10, .L1042+4
.LBE436:
.LBE435:
	.loc 5 395 0
	bne	.L1032
.LVL890:
.L1031:
	.loc 5 415 0
	mov	r0, #0
.LVL891:
	sub	sp, fp, #40
.LCFI318:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL892:
.L1038:
.LCFI319:
.LBB438:
.LBB437:
	.loc 5 397 0
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-48]
	mov	r8, #0
	ldrb	r0, [r3, #36]	@ zero_extendqisi2
.LVL893:
	bl	V2P_block
.LVL894:
	mov	r9, r0
.L1033:
	.loc 5 398 0
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L1037
	add	r7, r7, #1
.LVL895:
.L1032:
	.loc 5 396 0
	ldrh	r2, [r6, #8]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1038
	.loc 5 412 0
	bl	FtlGcReFreshBadBlk
.LVL896:
	b	.L1031
.L1037:
	.loc 5 399 0
	uxth	r3, r8
	ldr	r5, .L1042+8
	lsl	r1, r3, #1
	ldrh	r1, [r5, r1]
	cmp	r1, r9
	bne	.L1034
	.loc 5 401 0
	mov	r1, r9
	ldr	r0, .L1042+12
	str	r3, [fp, #-52]
	bl	printk
.LVL897:
	.loc 5 403 0
	mov	r0, r9
	bl	FtlBbmMapBadBlock
.LVL898:
	.loc 5 404 0
	bl	FtlBbmTblFlush
.LVL899:
	ldr	r3, [fp, #-52]
	.loc 5 405 0
	ldrh	r1, [r10]
	add	r3, r5, r3, lsl #1
.L1035:
	cmp	r4, r1
	bcc	.L1036
	.loc 5 407 0
	sub	r1, r1, #1
	strh	r1, [r10]	@ movhi
.L1034:
	add	r8, r8, #1
	b	.L1033
.L1036:
	.loc 5 406 0
	ldrh	r0, [r3, #2]!
	.loc 5 405 0
	add	r4, r4, #1
.LVL900:
	uxth	r4, r4
.LVL901:
	.loc 5 406 0
	strh	r0, [r3, #-2]	@ movhi
	b	.L1035
.L1043:
	.align	2
.L1042:
	.word	.LANCHOR0
	.word	.LANCHOR0+3186
	.word	.LANCHOR0+3188
	.word	.LC93
.LBE437:
.LBE438:
.LFE257:
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
.LFB241:
	.loc 2 2687 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL902:
	.movsp ip
	mov	ip, sp
.LCFI320:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI321:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI322:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2687 0
	.loc 2 2688 0
	lsl	r3, r0, #1
	.loc 2 2687 0
	mov	r5, r0
	.loc 2 2688 0
	ldr	r2, .L1054
	ldr	r1, [r2, #2260]
	mov	r4, r2
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L1045
	.loc 2 2689 0
	movw	r1, #2568
	ldrh	r0, [r2, r1]
	cmp	r0, r5
	.loc 2 2691 0
	mvneq	r3, #0
	strheq	r3, [r2, r1]	@ movhi
	.loc 2 2689 0
	beq	.L1047
	.loc 2 2692 0
	movw	r1, #2280
	ldrh	r1, [r2, r1]
	cmp	r1, r5
	beq	.L1044
	.loc 2 2692 0 is_stmt 0 discriminator 1
	movw	r1, #2328
	ldrh	r1, [r2, r1]
	cmp	r1, r5
	beq	.L1044
	.loc 2 2692 0 discriminator 2
	movw	r1, #2376
	ldrh	r2, [r2, r1]
	cmp	r2, r5
	beq	.L1044
.L1047:
.LBB441:
.LBB442:
	.loc 2 2696 0 is_stmt 1
	mov	r1, r5
	ldr	r0, .L1054+4
	bl	List_remove_node
.LVL903:
	movw	r3, #2268
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1049
	movw	r2, #2696
	ldr	r1, .L1054+8
	ldr	r0, .L1054+12
	bl	printk
.LVL904:
.L1049:
	movw	r6, #2268
	.loc 2 2697 0
	mov	r0, r5
	.loc 2 2696 0
	ldrh	r3, [r4, r6]
	sub	r3, r3, #1
	strh	r3, [r4, r6]	@ movhi
	.loc 2 2697 0
	bl	free_data_superblock
.LVL905:
	.loc 2 2698 0
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
.LVL906:
	.loc 2 2699 0
	movw	r3, #2276
	ldrh	r2, [r4, r6]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	ldrh	r2, [r4, #16]
	cmp	r3, r2
	ble	.L1053
	movw	r2, #2699
	ldr	r1, .L1054+8
	ldr	r0, .L1054+12
	bl	printk
.LVL907:
.L1053:
	mov	r3, #1
	b	.L1044
.LVL908:
.L1045:
.LBE442:
.LBE441:
	.loc 2 2702 0
	bl	List_update_data_list
.LVL909:
	.loc 2 2704 0
	mov	r3, #0
.L1044:
	.loc 2 2705 0
	mov	r0, r3
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1055:
	.align	2
.L1054:
	.word	.LANCHOR0
	.word	.LANCHOR0+2256
	.word	.LANCHOR1+385
	.word	.LC1
.LFE241:
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.syntax unified
	.arm
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
.LFB242:
	.loc 2 2708 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL910:
	.movsp ip
	mov	ip, sp
.LCFI323:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI324:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI325:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2708 0
	.loc 2 2711 0
	movw	r3, #65535
	.loc 2 2708 0
	mov	r5, r0
	.loc 2 2711 0
	cmp	r0, r3
	ldr	r6, .L1068
	beq	.L1057
	.loc 2 2712 0
	ldr	r3, [r6, #2260]
	lsl	r4, r0, #1
	ldrh	r2, [r3, r4]
	cmp	r2, #0
	.loc 2 2717 0
	subne	r2, r2, #1
	strhne	r2, [r3, r4]	@ movhi
	.loc 2 2712 0
	bne	.L1057
	.loc 2 2713 0
	mov	r1, r0
	ldr	r0, .L1068+4
	bl	printk
.LVL911:
	.loc 2 2714 0
	ldr	r3, [r6, #2260]
	ldrh	r4, [r3, r4]
	cmp	r4, #0
	beq	.L1059
.L1066:
	.loc 2 2723 0
	mov	r4, #0
.L1056:
	.loc 2 2733 0
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1059:
	.loc 2 2714 0 discriminator 1
	movw	r2, #2714
.L1067:
	.loc 2 2730 0 discriminator 2
	ldr	r1, .L1068+8
	ldr	r0, .L1068+12
	bl	printk
.LVL912:
	b	.L1056
.L1057:
	.loc 2 2721 0
	ldr	r7, .L1068+16
	movw	r3, #65535
	ldrh	r0, [r7]
.LVL913:
	cmp	r0, r3
	.loc 2 2722 0
	strheq	r5, [r7]	@ movhi
	beq	.L1066
.L1061:
	.loc 2 2724 0
	cmp	r5, r0
	beq	.L1066
	.loc 2 2727 0
	bl	update_vpc_list
.LVL914:
	.loc 2 2730 0
	ldr	r2, [r6, #2252]
	.loc 2 2727 0
	adds	r4, r0, #0
	.loc 2 2730 0
	ldr	r3, [r6, #2256]
	.loc 2 2727 0
	movne	r4, #1
	.loc 2 2730 0
	ldr	r1, [r6, #2260]
	.loc 2 2729 0
	strh	r5, [r7]	@ movhi
	.loc 2 2730 0
	sub	r3, r3, r2
	asr	r2, r3, #1
	ldr	r3, .L1068+20
	mul	r3, r3, r2
	uxth	r2, r3
	uxth	r3, r3
	lsl	r2, r2, #1
	cmp	r3, r5
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L1056
	.loc 2 2730 0 is_stmt 0 discriminator 2
	movw	r2, #2730
	b	.L1067
.L1069:
	.align	2
.L1068:
	.word	.LANCHOR0
	.word	.LC94
	.word	.LANCHOR1+401
	.word	.LC1
	.word	.LANCHOR0+3952
	.word	-1431655765
.LFE242:
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	.fnstart
.LFB208:
	.loc 2 947 0 is_stmt 1
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI326:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI327:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI328:
	.pad #28
	sub	sp, sp, #28
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 947 0
	.loc 2 948 0
	ldr	r6, .L1088
	ldr	r4, .L1088+4
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L1071
	.loc 2 948 0 is_stmt 0 discriminator 1
	ldrb	r3, [r4, #2288]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1071
	.loc 2 949 0 is_stmt 1
	ldrb	r1, [r4, #2287]	@ zero_extendqisi2
	ldrh	r3, [r4, #78]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1071
.LBB446:
.LBB447:
	.loc 2 956 0
	ldrb	r8, [r4, #2290]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1070
	.loc 2 951 0
	ldr	r7, [r4, #2436]
	.loc 2 958 0
	mov	r2, r8
	sub	r1, fp, #68
	.loc 2 953 0
	ldrh	r9, [r4, #8]
	.loc 2 951 0
	sub	r7, r7, #1
.LVL915:
	.loc 2 958 0
	mov	r0, r7
	bl	log2phys
.LVL916:
	.loc 2 961 0
	ldr	r3, [fp, #-68]
	.loc 2 964 0
	ldr	r5, [r4, #3244]
	.loc 2 963 0
	ldr	r0, [r4, #2244]
	.loc 2 967 0
	cmn	r3, #1
	.loc 2 961 0
	str	r3, [fp, #-60]
	.loc 2 962 0
	str	r7, [fp, #-48]
	.loc 2 963 0
	str	r0, [fp, #-56]
	.loc 2 964 0
	str	r5, [fp, #-52]
	.loc 2 966 0
	str	r8, [r5, #4]
	.loc 2 967 0
	beq	.L1073
	.loc 2 969 0
	mov	r2, r8
	mov	r1, #1
	sub	r0, fp, #64
	bl	FlashReadPages
.LVL917:
.L1074:
	.loc 2 975 0
	ldr	r3, .L1088+8
	mov	r8, #0
	.loc 2 953 0
	lsl	r9, r9, #2
.LVL918:
	.loc 2 987 0
	mov	r10, r8
	.loc 2 975 0
	strh	r3, [r5]	@ movhi
.L1075:
	.loc 2 976 0
	cmp	r9, r8
	bne	.L1079
.L1076:
	.loc 2 992 0
	mov	r3, #1
.L1087:
.LBE447:
.LBE446:
	.loc 2 998 0
	strb	r3, [r4, #2290]
.L1070:
	.loc 2 1000 0
	sub	sp, fp, #40
.LCFI329:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL919:
.L1073:
.LCFI330:
.LBB449:
.LBB448:
	.loc 2 973 0
	ldrh	r2, [r4, #86]
	mov	r1, #255
	bl	ftl_memset
.LVL920:
	b	.L1074
.LVL921:
.L1079:
	.loc 2 978 0
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L1076
	.loc 2 983 0
	ldr	r3, [fp, #-60]
	add	r8, r8, #1
	.loc 2 982 0
	str	r7, [r5, #8]
	.loc 2 986 0
	ldr	r0, .L1088
	.loc 2 983 0
	str	r3, [r5, #12]
	.loc 2 984 0
	ldrh	r3, [r6]
	strh	r3, [r5, #2]	@ movhi
	.loc 2 986 0
	bl	get_new_active_ppa
.LVL922:
	.loc 2 987 0
	ldr	r3, [r4, #2476]
	.loc 2 988 0
	mov	r1, #1
	.loc 2 986 0
	str	r0, [fp, #-60]
	.loc 2 988 0
	sub	r0, fp, #64
	.loc 2 987 0
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r10
	str	r3, [r4, #2476]
	.loc 2 988 0
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
.LVL923:
	.loc 2 990 0
	ldrh	r0, [r6]
	bl	decrement_vpc_count
.LVL924:
	b	.L1075
.LVL925:
.L1071:
.LBE448:
.LBE449:
	.loc 2 998 0
	mov	r3, #0
	b	.L1087
.L1089:
	.align	2
.L1088:
	.word	.LANCHOR0+2280
	.word	.LANCHOR0
	.word	-3947
.LFE208:
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	l2p_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
.LFB201:
	.loc 2 740 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI331:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI332:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI333:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 740 0
	.loc 2 746 0
	mov	r4, #0
	.loc 2 747 0
	mov	r6, #12
	.loc 2 746 0
	ldr	r5, .L1094
	.loc 2 744 0
	bl	FtlWriteDump_data
.LVL926:
.L1091:
	.loc 2 746 0 discriminator 1
	ldrh	r3, [r5, #114]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1093
	.loc 2 750 0
	mov	r0, #0
.LVL927:
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL928:
.L1093:
	.loc 2 747 0
	ldr	r2, [r5, #2424]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1092
	.loc 2 748 0
	bl	flush_l2p_region
.LVL929:
.L1092:
	add	r4, r4, #1
	b	.L1091
.L1095:
	.align	2
.L1094:
	.word	.LANCHOR0
.LFE201:
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
.LFB221:
	.loc 2 1656 0
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL930:
	.movsp ip
	mov	ip, sp
.LCFI334:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI335:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI336:
	.pad #60
	sub	sp, sp, #60
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1656 0
	.loc 2 1670 0
	ldrh	r3, [r0]
	movw	r2, #65535
	.loc 2 1656 0
	mov	r4, r0
	.loc 2 1670 0
	cmp	r3, r2
	beq	.L1217
	.loc 2 1675 0
	ldrh	r3, [r0, #2]
.LVL931:
	.loc 2 1681 0
	ldr	r6, .L1225
	.loc 2 1675 0
	str	r3, [fp, #-68]
	.loc 2 1676 0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
.LVL932:
	.loc 2 1681 0
	ldr	r1, [fp, #-68]
.LVL933:
	.loc 2 1676 0
	str	r3, [fp, #-88]
	.loc 2 1681 0
	ldrh	r3, [r6, #78]
.LVL934:
	cmp	r3, r1
	mov	r3, #0
	.loc 2 1682 0
	strheq	r3, [r0, #4]	@ movhi
	.loc 2 1689 0
	ldrhne	r0, [r0, #16]
.LVL935:
	.loc 2 1681 0
	bne	.L1100
.LVL936:
.L1223:
	.loc 2 1989 0
	strb	r3, [r4, #6]
.L1217:
	.loc 2 2010 0
	mov	r0, #0
	sub	sp, fp, #40
.LCFI337:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL937:
.L1101:
.LCFI338:
	.loc 2 1692 0
	uxth	r1, r3
.LVL938:
	add	r1, r4, r1, lsl #1
	ldrh	r0, [r1, #16]
.LVL939:
.L1100:
	.loc 2 1690 0
	cmp	r0, r2
	uxth	r7, r3
	add	r3, r3, #1
	beq	.L1101
	.loc 2 1697 0
	mov	r1, #1
.LVL940:
	bl	FtlGetLastWrittenPage
.LVL941:
	.loc 2 1705 0
	cmn	r0, #1
	.loc 2 1697 0
	mov	r5, r0
.LVL942:
	.loc 2 1705 0
	beq	.L1102
	.loc 2 1717 0
	ldrh	r3, [r6, #8]
	.loc 2 1722 0
	mov	r2, #0
	.loc 2 1720 0
	ldr	r0, [r6, #3792]
	.loc 2 1719 0
	movw	r8, #65535
	.loc 2 1722 0
	ldr	lr, [r6, #3156]
	.loc 2 1720 0
	mov	r9, #20
	.loc 2 1717 0
	str	r3, [fp, #-64]
	.loc 2 1721 0
	ldr	r3, [r6, #3152]
	.loc 2 1722 0
	ldrh	r7, [r6, #88]
	.loc 2 1721 0
	str	r3, [fp, #-72]
	ldrh	r3, [r6, #86]
	.loc 2 1722 0
	mov	r6, r2
	.loc 2 1721 0
	str	r3, [fp, #-76]
	add	r3, r4, #16
	.loc 2 1722 0
	mov	ip, r3
	str	r3, [fp, #-84]
.L1103:
	.loc 2 1717 0 discriminator 1
	ldr	r1, [fp, #-64]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1107
	.loc 2 1728 0
	ldr	r7, .L1225
	.loc 2 1727 0
	mov	r2, #0
	mov	r1, r6
	.loc 2 1729 0
	mov	r8, #0
	.loc 2 1727 0
	bl	FlashReadPages
.LVL943:
	.loc 2 1728 0
	ldr	r3, [r7, #2476]
	.loc 2 1666 0
	movw	r2, #65535
	str	r2, [fp, #-76]
	.loc 2 1742 0
	uxth	r2, r5
	.loc 2 1728 0
	sub	r3, r3, #1
.LVL944:
	.loc 2 1742 0
	str	r2, [fp, #-72]
	.loc 2 1728 0
	str	r3, [fp, #-64]
	.loc 2 1730 0
	ldr	r3, [r7, #3792]
.LVL945:
	mov	r10, r3
.L1108:
	uxth	r2, r8
.LVL946:
	.loc 2 1729 0 discriminator 1
	cmp	r6, r2
	bhi	.L1113
	.loc 2 1747 0
	addeq	r2, r5, #1
.LVL947:
	uxtheq	r9, r2
.LVL948:
	.loc 2 1745 0
	bne	.L1111
.L1218:
	.loc 2 1753 0
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
.LVL949:
	.loc 2 1758 0
	ldrh	r3, [r7, #78]
	.loc 2 1753 0
	str	r0, [fp, #-72]
	.loc 2 1764 0
	ldr	r2, [fp, #-72]
.LVL950:
	.loc 2 1758 0
	cmp	r3, r9
	.loc 2 1764 0
	ldr	r1, [fp, #-88]
.LVL951:
	.loc 2 1760 0
	moveq	r3, #0
	.loc 2 1759 0
	strheq	r9, [r4, #2]	@ movhi
	.loc 2 1760 0
	strbeq	r3, [r4, #6]
	.loc 2 1761 0
	strheq	r3, [r4, #4]	@ movhi
	.loc 2 1764 0
	ldr	r3, [fp, #-68]
.LVL952:
	cmp	r9, r3
	cmpeq	r2, r1
	bne	.L1116
.LVL953:
.L1224:
	.loc 2 2004 0
	mov	r1, r9
.LVL954:
	mov	r0, r4
	bl	ftl_sb_update_avl_pages
.LVL955:
	b	.L1217
.LVL956:
.L1102:
	.loc 2 1706 0
	ldr	r3, [fp, #-68]
.LVL957:
	cmp	r3, #0
	beq	.L1104
	.loc 2 1706 0 is_stmt 0 discriminator 1
	movw	r2, #1706
	ldr	r1, .L1225+4
	ldr	r0, .L1225+8
	bl	printk
.LVL958:
.L1104:
	.loc 2 1707 0 is_stmt 1
	ldr	r3, [fp, #-88]
.LVL959:
	cmp	r3, #0
	cmpne	r7, r3
	beq	.L1105
	.loc 2 1707 0 is_stmt 0 discriminator 1
	movw	r2, #1707
	ldr	r1, .L1225+4
	ldr	r0, .L1225+8
	bl	printk
.LVL960:
.L1105:
	.loc 2 1708 0 is_stmt 1
	mov	r3, #0
.LVL961:
	strh	r3, [r4, #2]	@ movhi
	b	.L1223
.L1107:
	.loc 2 1718 0
	ldrh	r3, [ip], #2
	.loc 2 1719 0
	cmp	r3, r8
	beq	.L1106
	.loc 2 1720 0
	mla	r1, r9, r6, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	.loc 2 1721 0
	ldr	r3, [fp, #-76]
	mul	r3, r3, r6
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [fp, #-72]
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r1, #8]
	.loc 2 1722 0
	mul	r3, r7, r6
	.loc 2 1723 0
	add	r6, r6, #1
	uxth	r6, r6
	.loc 2 1722 0
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L1106:
	add	r2, r2, #1
	b	.L1103
.LVL962:
.L1113:
	.loc 2 1730 0
	ldr	r2, [r10]
.LVL963:
	cmp	r2, #0
	bne	.L1109
	.loc 2 1731 0
	ldr	r2, [r10, #12]
	.loc 2 1732 0
	ldr	r9, [r2, #4]
	cmn	r9, #1
	beq	.L1110
	str	r3, [fp, #-80]
	.loc 2 1733 0
	mov	r0, r9
	ldr	r3, .L1225
	str	r2, [fp, #-92]
	ldr	r1, [r3, #2476]
	bl	ftl_cmp_data_ver
.LVL964:
	ldr	r3, [fp, #-80]
	cmp	r0, #0
	ldr	r2, [fp, #-92]
	.loc 2 1735 0
	addne	r9, r9, #1
	ldrne	r1, .L1225
	strne	r9, [r1, #2476]
.L1110:
	.loc 2 1738 0
	ldr	r2, [r2]
	cmn	r2, #1
	bne	.L1112
.LVL965:
.L1111:
	.loc 2 1753 0
	uxth	r8, r8
	mov	r2, #20
	.loc 2 1752 0
	uxth	r9, r5
	.loc 2 1753 0
	mla	r3, r2, r8, r3
	b	.L1218
.LVL966:
.L1109:
	.loc 2 1742 0
	ldr	r2, [fp, #-72]
	str	r2, [fp, #-76]
.L1112:
	add	r8, r8, #1
	add	r10, r10, #20
	b	.L1108
.LVL967:
.L1116:
	.loc 2 1770 0
	ldr	r2, [fp, #-76]
.LVL968:
	movw	r3, #65535
.LVL969:
	cmp	r2, r3
	bne	.L1117
	.loc 2 1770 0 is_stmt 0 discriminator 1
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1118
.L1117:
.LBB450:
	.loc 2 1775 0 is_stmt 1
	ldr	r3, [r7, #3964]
	.loc 2 1774 0
	uxth	r8, r5
	.loc 2 1778 0
	uxth	r5, r5
.LVL970:
	.loc 2 1775 0
	cmn	r3, #1
	.loc 2 1776 0
	ldreq	r3, [fp, #-64]
.LVL971:
	streq	r3, [r7, #3964]
	.loc 2 1778 0
	ldr	r3, [fp, #-68]
.LVL972:
	ldr	r7, .L1225
	add	r3, r3, #7
.LVL973:
	cmp	r5, r3
	.loc 2 1779 0
	subgt	r5, r8, #7
	ldrle	r5, [fp, #-68]
	uxthgt	r5, r5
.L1121:
	.loc 2 1780 0 discriminator 1
	cmp	r5, r8
	bhi	.L1128
	.loc 2 1782 0
	ldrh	r3, [r7, #8]
	.loc 2 1784 0
	movw	lr, #65535
	.loc 2 1785 0
	ldr	r0, [r7, #3792]
	mov	ip, #20
	ldr	r1, [fp, #-84]
.LVL974:
	.loc 2 1782 0
	str	r3, [fp, #-76]
	.loc 2 1785 0
	mov	r3, #0
	mov	r6, r3
	b	.L1129
.L1123:
	.loc 2 1783 0
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	.loc 2 1784 0
	cmp	r2, lr
	.loc 2 1785 0
	mlane	r10, ip, r6, r0
	.loc 2 1786 0
	addne	r6, r6, #1
	.loc 2 1785 0
	orrne	r2, r5, r2, lsl #10
	.loc 2 1786 0
	uxthne	r6, r6
	.loc 2 1785 0
	strne	r2, [r10, #4]
.L1129:
	.loc 2 1782 0 discriminator 1
	ldr	r10, [fp, #-76]
	uxth	r2, r3
	cmp	r10, r2
	bhi	.L1123
	.loc 2 1789 0
	mov	r1, r6
	mov	r2, #0
	bl	FlashReadPages
.LVL975:
	.loc 2 1791 0
	ldr	r3, [r7, #3792]
	mov	r2, #20
	.loc 2 1793 0
	movw	r1, #65535
	mla	r6, r2, r6, r3
.L1124:
	.loc 2 1790 0 discriminator 1
	cmp	r6, r3
	.loc 2 1780 0
	addeq	r5, r5, #1
	uxtheq	r5, r5
	beq	.L1121
.L1127:
	.loc 2 1791 0
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1118
	.loc 2 1792 0
	ldr	r2, [r3, #12]
	.loc 2 1793 0
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1126
	.loc 2 1795 0
	ldr	r2, [r2, #4]
	cmn	r2, #1
	.loc 2 1797 0
	strne	r2, [r7, #3964]
.L1126:
	add	r3, r3, #20
	b	.L1124
.LVL976:
.L1128:
	.loc 2 1809 0
	mvn	r3, #0
	str	r3, [r7, #3964]
.LVL977:
.L1118:
.LBE450:
	.loc 2 1814 0
	ldr	r5, .L1225+12
.LVL978:
	mov	r3, #1
	mov	r0, r5
	.loc 2 1821 0
	sub	r5, r5, #3968
	.loc 2 1814 0
	strh	r3, [r0], #-104	@ movhi
	.loc 2 1815 0
	bl	FtlMapBlkWriteDump_data
.LVL979:
	ldr	r10, [fp, #-68]
.LVL980:
.L1130:
	.loc 2 1821 0
	ldrh	ip, [r5, #8]
	.loc 2 1823 0
	movw	lr, #65535
	.loc 2 1824 0
	ldr	r0, [r5, #3792]
	mov	r6, #20
.LVL981:
	.loc 2 1821 0
	ldr	r1, [fp, #-84]
	mov	r3, #0
	.loc 2 1820 0
	str	r3, [fp, #-76]
.L1131:
	.loc 2 1821 0 discriminator 1
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1133
	.loc 2 1828 0
	mov	r2, #0
	ldr	r1, [fp, #-76]
	bl	FlashReadPages
.LVL982:
	.loc 2 1829 0
	mov	r3, #0
.LVL983:
.L1222:
	str	r3, [fp, #-80]
	.loc 2 1829 0 is_stmt 0 discriminator 2
	ldr	r2, [fp, #-76]
.LVL984:
	ldrh	r3, [fp, #-80]
	cmp	r2, r3
	bhi	.L1161
	.loc 2 1982 0 is_stmt 1
	add	r3, r10, #1
	uxth	r10, r3
	.loc 2 1983 0
	ldrh	r3, [r5, #78]
	cmp	r3, r10
	bne	.L1130
	.loc 2 1986 0
	ldrh	r2, [r5, #8]
	.loc 2 1988 0
	movw	r0, #65535
	.loc 2 1985 0
	mov	r3, #0
	.loc 2 1984 0
	strh	r10, [r4, #2]	@ movhi
	.loc 2 1985 0
	strh	r3, [r4, #4]	@ movhi
.L1162:
	.loc 2 1986 0 discriminator 1
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1217
	.loc 2 1987 0
	ldr	r1, [fp, #-84]
	ldrh	ip, [r1], #2
	.loc 2 1988 0
	cmp	ip, r0
	.loc 2 1987 0
	str	r1, [fp, #-84]
	add	r1, r3, #1
	.loc 2 1988 0
	bne	.L1223
	mov	r3, r1
	b	.L1162
.LVL985:
.L1133:
	.loc 2 1822 0
	ldrh	r2, [r1], #2
	.loc 2 1823 0
	cmp	r2, lr
	beq	.L1132
	.loc 2 1824 0
	ldr	r7, [fp, #-76]
.LVL986:
	orr	r2, r10, r2, lsl #10
	mla	r7, r6, r7, r0
	str	r2, [r7, #4]
	.loc 2 1825 0
	ldr	r2, [fp, #-76]
	add	r2, r2, #1
	uxth	r2, r2
	str	r2, [fp, #-76]
.LVL987:
.L1132:
	add	r3, r3, #1
	b	.L1131
.LVL988:
.L1161:
	ldr	r3, [fp, #-80]
	mov	r7, #20
.LVL989:
	.loc 2 1830 0
	ldr	r2, [r5, #3792]
	mul	r7, r7, r3
	str	r2, [fp, #-92]
	add	r8, r2, r7
	ldr	r6, [r8, #4]
.LVL990:
	.loc 2 1831 0
	ubfx	r0, r6, #10, #16
	.loc 2 1830 0
	str	r6, [fp, #-48]
	.loc 2 1831 0
	bl	P2V_plane
.LVL991:
	.loc 2 1833 0
	ldr	r3, [fp, #-68]
.LVL992:
	cmp	r10, r3
	bcc	.L1135
	.loc 2 1833 0 is_stmt 0 discriminator 1
	ldr	r2, [fp, #-88]
.LVL993:
	.loc 2 1834 0 is_stmt 1 discriminator 1
	moveq	r3, #1
.LVL994:
	movne	r3, #0
	.loc 2 1833 0 discriminator 1
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1135
	.loc 2 1837 0
	ldr	r3, [fp, #-72]
.LVL995:
	cmp	r10, r9
	cmpeq	r3, r0
	beq	.L1136
	.loc 2 1841 0
	ldr	r2, [fp, #-92]
.LVL996:
	ldr	r3, [r2, r7]
.LVL997:
	cmn	r3, #1
	beq	.L1137
	.loc 2 1842 0
	ldr	r8, [r8, #12]
	.loc 2 1843 0
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1138
.LVL998:
.L1145:
.LBB451:
	.loc 2 1876 0
	ldrh	r0, [r4]
.LVL999:
.L1221:
.LBE451:
	.loc 2 1960 0
	bl	decrement_vpc_count
.LVL1000:
.L1135:
	ldr	r3, [fp, #-80]
.LVL1001:
	add	r3, r3, #1
	b	.L1222
.LVL1002:
.L1138:
	.loc 2 1849 0
	ldr	r3, [r8, #4]
.LVL1003:
	.loc 2 1850 0
	cmn	r3, #1
	.loc 2 1849 0
	str	r3, [fp, #-64]
	.loc 2 1850 0
	beq	.L1139
	.loc 2 1851 0 discriminator 1
	mov	r0, r3
	ldr	r1, [r5, #2476]
	bl	ftl_cmp_data_ver
.LVL1004:
	.loc 2 1850 0 discriminator 1
	cmp	r0, #0
	.loc 2 1853 0 discriminator 1
	ldrne	r3, [fp, #-64]
.LVL1005:
	addne	r3, r3, #1
.LVL1006:
	strne	r3, [r5, #2476]
.LVL1007:
.L1139:
	.loc 2 1856 0
	ldrh	r2, [r8]
	movw	r3, #61589
.LVL1008:
	cmp	r2, r3
	beq	.L1140
	.loc 2 1856 0 is_stmt 0 discriminator 1
	mov	r2, #1856
	ldr	r1, .L1225+4
	ldr	r0, .L1225+8
	bl	printk
.LVL1009:
.L1140:
	.loc 2 1857 0 is_stmt 1
	ldr	r6, [r8, #8]
.LVL1010:
	.loc 2 1859 0
	sub	r1, fp, #52
	.loc 2 1858 0
	ldr	r3, [r8, #12]
	.loc 2 1859 0
	mov	r2, #0
	mov	r0, r6
	.loc 2 1858 0
	str	r3, [fp, #-56]
	.loc 2 1859 0
	bl	log2phys
.LVL1011:
	.loc 2 1860 0
	ldr	r1, [r5, #3964]
	cmn	r1, #1
	beq	.L1141
	.loc 2 1861 0 discriminator 1
	ldr	r0, [fp, #-64]
	bl	ftl_cmp_data_ver
.LVL1012:
	.loc 2 1860 0 discriminator 1
	cmp	r0, #0
	beq	.L1141
.LBB452:
	.loc 2 1865 0
	ldr	r3, [fp, #-56]
	cmn	r3, #1
	beq	.L1142
	.loc 2 1866 0
	ldr	r0, [r5, #3792]
	.loc 2 1868 0
	mov	r2, #0
	mov	r1, #1
	.loc 2 1866 0
	add	r0, r0, r7
	.loc 2 1867 0
	str	r3, [r0, #4]
	.loc 2 1866 0
	ldr	r8, [r0, #12]
	.loc 2 1868 0
	bl	FlashReadPages
.LVL1013:
	.loc 2 1881 0
	ldr	r3, [r5, #3792]
	add	r2, r3, r7
	str	r2, [fp, #-92]
	ldr	r2, [r3, r7]
	cmn	r2, #1
	bne	.L1143
.LVL1014:
.L1144:
	.loc 2 1913 0
	mvn	r3, #0
	.loc 2 1914 0
	ldrh	r0, [r4]
	.loc 2 1913 0
	str	r3, [fp, #-56]
	.loc 2 1914 0
	bl	decrement_vpc_count
.LVL1015:
.L1152:
.LBE452:
	.loc 2 1954 0
	ldr	r8, [fp, #-56]
	cmn	r8, #1
	beq	.L1135
.L1165:
	.loc 2 1956 0
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
.LVL1016:
	.loc 2 1957 0
	ldrh	r3, [r5, #16]
	.loc 2 1956 0
	mov	r6, r0
.LVL1017:
	.loc 2 1957 0
	cmp	r3, r0
	bhi	.L1157
	.loc 2 1957 0 is_stmt 0 discriminator 1
	movw	r2, #1957
	ldr	r1, .L1225+4
	ldr	r0, .L1225+8
	bl	printk
.LVL1018:
.L1157:
	.loc 2 1958 0 is_stmt 1
	ldr	r2, [r5, #2260]
	lsl	r3, r6, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1158
	.loc 2 1960 0
	mov	r0, r6
	b	.L1221
.LVL1019:
.L1142:
.LBB453:
	.loc 2 1872 0
	ldr	r3, [fp, #-48]
	ldr	r2, [fp, #-52]
	cmp	r2, r3
	bne	.L1145
	.loc 2 1873 0
	mov	r2, #1
	sub	r1, fp, #56
	mov	r0, r6
	bl	log2phys
.LVL1020:
	b	.L1145
.L1143:
	.loc 2 1881 0 discriminator 1
	ldr	r2, [r8, #8]
	cmp	r6, r2
	bne	.L1144
	str	r3, [fp, #-100]
	.loc 2 1869 0 discriminator 2
	ldr	r3, [r8, #4]
.LVL1021:
	.loc 2 1882 0 discriminator 2
	ldr	r0, [r5, #3964]
	mov	r1, r3
	.loc 2 1869 0 discriminator 2
	str	r3, [fp, #-96]
	.loc 2 1882 0 discriminator 2
	bl	ftl_cmp_data_ver
.LVL1022:
	.loc 2 1881 0 discriminator 2
	cmp	r0, #0
	beq	.L1144
	.loc 2 1883 0
	ldr	r1, [fp, #-48]
	ldr	r2, [fp, #-52]
	ldr	r3, [fp, #-100]
	cmp	r2, r1
	.loc 2 1885 0
	ldr	r1, [fp, #-56]
	.loc 2 1883 0
	bne	.L1147
.LVL1023:
.L1219:
	.loc 2 1909 0
	mov	r0, r6
	bl	FtlReUsePrevPpa
.LVL1024:
	b	.L1144
.LVL1025:
.L1147:
	.loc 2 1886 0
	cmp	r2, r1
	beq	.L1144
	.loc 2 1887 0
	cmn	r2, #1
	.loc 2 1892 0
	streq	r2, [r3, r7]
	.loc 2 1887 0
	beq	.L1149
	.loc 2 1888 0
	ldr	r3, [fp, #-92]
	.loc 2 1890 0
	mov	r1, #1
	.loc 2 1889 0
	str	r2, [r3, #4]
	.loc 2 1890 0
	mov	r0, r3
	mov	r2, #0
	.loc 2 1888 0
	ldr	r8, [r3, #12]
	.loc 2 1890 0
	bl	FlashReadPages
.LVL1026:
.L1149:
	.loc 2 1895 0
	ldr	r3, [r5, #3792]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1150
	.loc 2 1896 0
	ldr	r7, [r8, #4]
.LVL1027:
	.loc 2 1897 0
	ldr	r0, [r5, #3964]
	mov	r1, r7
	bl	ftl_cmp_data_ver
.LVL1028:
	cmp	r0, #0
	beq	.L1150
	.loc 2 1898 0
	mov	r1, r7
	ldr	r0, [fp, #-96]
	bl	ftl_cmp_data_ver
.LVL1029:
	cmp	r0, #0
	beq	.L1144
.L1150:
	.loc 2 1909 0
	ldr	r1, [fp, #-56]
	b	.L1219
.LVL1030:
.L1141:
.LBE453:
	.loc 2 1915 0
	ldr	r3, [fp, #-48]
	ldr	r2, [fp, #-52]
	cmp	r2, r3
	beq	.L1152
	.loc 2 1917 0
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r6
	bl	log2phys
.LVL1031:
	.loc 2 1918 0
	ldr	r8, [fp, #-52]
	cmn	r8, #1
	beq	.L1152
	.loc 2 1918 0 is_stmt 0 discriminator 1
	ldr	r3, [fp, #-56]
	cmp	r8, r3
	beq	.L1165
	.loc 2 1919 0 is_stmt 1
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
.LVL1032:
	.loc 2 1921 0
	ldr	r3, .L1225+16
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L1156
	.loc 2 1921 0 is_stmt 0 discriminator 1
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L1156
	.loc 2 1922 0 is_stmt 1
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L1152
.L1156:
.LBB454:
	.loc 2 1927 0
	ldr	r0, [r5, #3792]
	.loc 2 1930 0
	mov	r2, #0
	mov	r1, #1
	.loc 2 1929 0
	str	r8, [r0, #4]
	.loc 2 1927 0
	ldr	r7, [r0, #12]
	.loc 2 1930 0
	bl	FlashReadPages
.LVL1033:
	.loc 2 1933 0
	ldr	r3, [r5, #3792]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1152
	.loc 2 1934 0
	ldr	r1, [r7, #4]
	ldr	r0, [fp, #-64]
	bl	ftl_cmp_data_ver
.LVL1034:
	cmp	r0, #0
	bne	.L1152
	.loc 2 1941 0
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r6
	bl	log2phys
.LVL1035:
	b	.L1152
.LVL1036:
.L1158:
.LBE454:
	.loc 2 1962 0
	mov	r1, r6
	ldr	r0, .L1225+20
	bl	printk
.LVL1037:
	b	.L1135
.LVL1038:
.L1137:
	.loc 2 1969 0
	ldr	r3, [r5, #3972]
	cmp	r3, #31
	.loc 2 1970 0
	addls	r2, r5, r3, lsl #2
	.loc 2 1971 0
	addls	r3, r3, #1
	strls	r3, [r5, #3972]
	.loc 2 1970 0
	strls	r6, [r2, #3976]
	.loc 2 1973 0
	ldrh	r0, [r4]
	bl	decrement_vpc_count
.LVL1039:
	.loc 2 1975 0
	ldr	r3, [r5, #3964]
	cmn	r3, #1
	.loc 2 1976 0
	ldreq	r3, [fp, #-64]
.LVL1040:
	.loc 2 1975 0
	beq	.L1220
	.loc 2 1977 0
	ldr	r2, [fp, #-64]
.LVL1041:
	cmp	r2, r3
	bcs	.L1135
	.loc 2 1978 0
	mov	r3, r2
.LVL1042:
.L1220:
	str	r3, [r5, #3964]
	b	.L1135
.LVL1043:
.L1136:
	.loc 2 2002 0
	ldrb	r3, [fp, #-72]	@ zero_extendqisi2
.LVL1044:
	.loc 2 2003 0
	strh	r9, [r4, #2]	@ movhi
	.loc 2 2004 0
	ldr	r2, [fp, #-72]
.LVL1045:
	.loc 2 2002 0
	strb	r3, [r4, #6]
	b	.L1224
.L1226:
	.align	2
.L1225:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3968
	.word	.LANCHOR0+2280
	.word	.LC95
.LFE221:
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
.LFB228:
	.loc 2 2156 0
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1046:
	.movsp ip
	mov	ip, sp
.LCFI339:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI340:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI341:
	.pad #24
	sub	sp, sp, #24
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2156 0
	ldr	r5, .L1238
	mov	r4, r0
	.loc 2 2162 0
	mov	r6, #7
	.loc 2 2168 0
	mvn	r7, #0
.L1228:
	.loc 2 2162 0
	subs	r6, r6, #1
	beq	.L1230
	.loc 2 2163 0
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1229
.L1230:
	.loc 2 2183 0
	ldrh	r3, [r4]
	ldr	r1, [r5, #2260]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	.loc 2 2184 0
	ldrh	r3, [r5, #78]
	strh	r3, [r4, #2]	@ movhi
	.loc 2 2185 0
	mov	r3, #0
	strb	r3, [r4, #6]
	.loc 2 2186 0
	strh	r3, [r4, #4]	@ movhi
	.loc 2 2188 0
	sub	sp, fp, #28
.LCFI342:
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL1047:
.L1229:
.LCFI343:
	.loc 2 2165 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1048:
	.loc 2 2166 0
	cmn	r0, #1
	.loc 2 2165 0
	str	r0, [fp, #-44]
	.loc 2 2166 0
	beq	.L1230
	.loc 2 2170 0
	ldr	r1, [r5, #3244]
	.loc 2 2179 0
	sub	r0, fp, #48
	.loc 2 2169 0
	ldr	r3, [r5, #2244]
	.loc 2 2168 0
	str	r7, [fp, #-32]
	.loc 2 2170 0
	str	r1, [fp, #-36]
	.loc 2 2169 0
	str	r3, [fp, #-40]
	.loc 2 2174 0
	str	r7, [r1, #12]
	.loc 2 2173 0
	str	r7, [r1, #8]
	.loc 2 2175 0
	ldrh	r3, [r4]
	strh	r3, [r1, #2]	@ movhi
	.loc 2 2176 0
	mov	r3, #0
	strh	r3, [r1]	@ movhi
	.loc 2 2178 0
	ldr	r2, [r5, #2476]
	str	r2, [r1, #4]
	add	r2, r2, #1
	cmn	r2, #1
	.loc 2 2179 0
	mov	r1, #1
	.loc 2 2178 0
	moveq	r2, r3
	str	r2, [r5, #2476]
	.loc 2 2179 0
	mov	r2, r3
	bl	FlashProgPages
.LVL1049:
	.loc 2 2180 0
	ldrh	r0, [r4]
	bl	decrement_vpc_count
.LVL1050:
	b	.L1228
.L1239:
	.align	2
.L1238:
	.word	.LANCHOR0
.LFE228:
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlLoadBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
.LFB169:
	.loc 4 173 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI344:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI345:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI346:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 173 0
	.loc 4 180 0
	ldr	r4, .L1272
	.loc 4 186 0
	ldr	r7, .L1272+4
	.loc 4 180 0
	ldr	r3, [r4, #2244]
	.loc 4 181 0
	ldr	r6, [r4, #3244]
	.loc 4 180 0
	str	r3, [r4, #3232]
	.loc 4 181 0
	str	r6, [r4, #3236]
	.loc 4 183 0
	bl	FtlBbtMemInit
.LVL1051:
	.loc 4 184 0
	ldrh	r5, [r4, #74]
	sub	r5, r5, #1
	uxth	r5, r5
.LVL1052:
.L1241:
	.loc 4 184 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #74]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L1244
	.loc 4 185 0 is_stmt 1
	lsl	r3, r5, #10
	.loc 4 186 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 185 0
	str	r3, [r4, #3228]
	.loc 4 186 0
	bl	FlashReadPages
.LVL1053:
	.loc 4 187 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	bne	.L1242
	.loc 4 188 0
	ldr	r3, [r4, #3228]
	.loc 4 189 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 4 188 0
	add	r3, r3, #1
	str	r3, [r4, #3228]
	.loc 4 189 0
	bl	FlashReadPages
.LVL1054:
.L1242:
	.loc 4 191 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	beq	.L1243
	.loc 4 196 0
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1243
	.loc 4 199 0
	ldr	r3, [r6, #4]
	.loc 4 198 0
	strh	r5, [r4, #128]	@ movhi
	.loc 4 199 0
	str	r3, [r4, #136]
	.loc 4 200 0
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #132]	@ movhi
.L1244:
	.loc 4 216 0
	ldrh	r3, [r4, #128]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1258
	.loc 4 222 0
	ldrh	r3, [r4, #132]
	cmp	r3, r2
	beq	.L1248
	.loc 4 224 0
	lsl	r3, r3, #10
	.loc 4 225 0
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1272+4
	.loc 4 224 0
	str	r3, [r4, #3228]
	.loc 4 225 0
	bl	FlashReadPages
.LVL1055:
	.loc 4 226 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	beq	.L1248
	.loc 4 229 0
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1248
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
.L1248:
	.loc 4 245 0
	ldr	r7, .L1272+4
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
.L1250:
	.loc 4 241 0
	cmp	r5, #0
	bge	.L1253
	.loc 4 254 0
	mov	r2, #254
	ldr	r1, .L1272+8
	ldr	r0, .L1272+12
	bl	printk
.LVL1058:
.L1252:
	.loc 4 255 0
	ldrh	r3, [r6, #10]
	.loc 4 257 0
	ldrh	r0, [r6, #12]
	.loc 4 255 0
	strh	r3, [r4, #134]	@ movhi
	.loc 4 257 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1255
	.loc 4 259 0
	ldr	r2, [r4, #4]
	cmp	r0, r2
	beq	.L1255
	.loc 4 259 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	lsr	r3, r3, #2
	.loc 4 261 0 is_stmt 1 discriminator 1
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1255
	.loc 4 263 0
	bl	FtlSysBlkNumInit
.LVL1059:
.L1255:
	ldr	r6, .L1272+16
	.loc 4 173 0 discriminator 1
	mov	r5, #0
.LVL1060:
	.loc 4 269 0 discriminator 1
	ldr	r7, .L1272+20
.L1256:
	.loc 4 268 0 discriminator 1
	ldrh	r3, [r4, #30]
	cmp	r5, r3
	bcc	.L1257
	.loc 4 275 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.LVL1061:
.L1243:
	.loc 4 184 0 discriminator 2
	sub	r5, r5, #1
.LVL1062:
	uxth	r5, r5
.LVL1063:
	b	.L1241
.LVL1064:
.L1253:
	.loc 4 243 0
	ldrh	r3, [r4, #128]
	.loc 4 245 0
	mov	r2, #1
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
.LVL1065:
	.loc 4 247 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	beq	.L1251
	.loc 4 247 0 is_stmt 0 discriminator 1
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1252
.L1251:
	sub	r5, r5, #1
.LVL1066:
	sxth	r5, r5
.LVL1067:
	b	.L1250
.LVL1068:
.L1257:
	.loc 4 269 0 is_stmt 1 discriminator 3
	ldrh	r2, [r7]
	ldr	r1, [r4, #3232]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	.loc 4 268 0 discriminator 3
	add	r5, r5, #1
	.loc 4 269 0 discriminator 3
	bl	ftl_memcpy
.LVL1069:
	b	.L1256
.LVL1070:
.L1258:
	.loc 4 218 0
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1273:
	.align	2
.L1272:
	.word	.LANCHOR0
	.word	.LANCHOR0+3224
	.word	.LANCHOR1+443
	.word	.LC1
	.word	.LANCHOR0+152
	.word	.LANCHOR0+3860
.LFE169:
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
.LFB171:
	.loc 4 317 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI347:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI348:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI349:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 4 317 0
	ldr	r8, .L1294
	.loc 4 327 0
	mov	r7, #0
.LVL1071:
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
.LVL1074:
.L1275:
	.loc 4 327 0 is_stmt 0 discriminator 1
	ldrh	r3, [r5, #30]
	cmp	r7, r3
	bcc	.L1281
	mov	r5, #0
.L1282:
	.loc 4 381 0 is_stmt 1 discriminator 1
	ldrh	r3, [r4, #90]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bhi	.L1283
	.loc 4 387 0
	ldrh	r5, [r4, #140]
	.loc 4 396 0
	movw	r6, #65535
.LVL1075:
	.loc 4 387 0
	sub	r5, r5, #1
	uxth	r5, r5
.L1284:
	.loc 4 387 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #140]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1288
	.loc 4 389 0 is_stmt 1
	mov	r0, r5
	bl	FtlBbmIsBadBlock
.LVL1076:
	cmp	r0, #1
	beq	.L1285
	.loc 4 391 0
	mov	r0, r5
	bl	FlashTestBlk
.LVL1077:
	cmp	r0, #0
	beq	.L1286
	.loc 4 393 0
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.LVL1078:
.L1285:
	.loc 4 387 0 discriminator 2
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1284
.LVL1079:
.L1281:
	.loc 4 331 0
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	.loc 4 328 0
	ldr	r0, [r5, #2244]
	.loc 4 329 0
	ldr	r10, [r5, #3244]
	.loc 4 331 0
	cmp	r3, r2
	.loc 4 328 0
	str	r0, [r5, #3232]
	.loc 4 329 0
	str	r10, [r5, #3236]
	.loc 4 331 0
	beq	.L1276
	.loc 4 333 0
	ldrh	r6, [r5, #74]
.LVL1080:
	.loc 4 335 0
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1294+4
	.loc 4 333 0
	mla	r6, r7, r6, r3
.LVL1081:
	.loc 4 334 0
	lsl	r3, r6, #10
	str	r3, [r5, #3228]
	.loc 4 335 0
	bl	FlashReadPages
.LVL1082:
	.loc 4 336 0
	ldrh	r2, [r5, #74]
	ldr	r1, [r5, #3232]
	ldr	r0, [r8]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
.LVL1083:
.L1277:
	.loc 4 376 0 discriminator 2
	uxth	r0, r6
	.loc 4 327 0 discriminator 2
	add	r7, r7, #1
	add	r8, r8, #4
	.loc 4 376 0 discriminator 2
	bl	FtlBbmMapBadBlock
.LVL1084:
	b	.L1275
.L1276:
	.loc 4 340 0
	mov	r1, r7
	bl	FlashGetBadBlockList
.LVL1085:
	.loc 4 341 0
	ldr	r1, [r8]
	ldr	r0, [r5, #3232]
	bl	FtlBbt2Bitmap
.LVL1086:
	.loc 4 343 0
	ldrh	r3, [r5, #74]
.L1293:
	.loc 4 347 0
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-48]
.L1278:
	.loc 4 345 0
	ldr	r3, [fp, #-48]
	ldrh	r0, [r4, #74]
	smlabb	r0, r0, r7, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
.LVL1087:
	cmp	r0, #1
	beq	.L1279
	.loc 4 349 0
	ldrh	r3, [fp, #-48]
.LBB465:
.LBB466:
.LBB467:
	.loc 1 202 0
	mov	r1, #16
	ldr	r0, [r4, #3244]
.LBE467:
.LBE466:
.LBE465:
	.loc 4 349 0
	strh	r3, [r9]	@ movhi
.LBB470:
.LBB469:
.LBB468:
	.loc 1 202 0
	bl	__memzero
.LVL1088:
.LBE468:
.LBE469:
.LBE470:
.LBB471:
.LBB472:
.LBB473:
	mov	r1, #4096
	ldr	r0, [r4, #2244]
	bl	__memzero
.LVL1089:
.LBE473:
.LBE472:
.LBE471:
	.loc 4 352 0
	ldr	r3, .L1294+8
	strh	r3, [r10]	@ movhi
	.loc 4 353 0
	mov	r3, #0
	str	r3, [r10, #4]
	.loc 4 354 0
	ldrh	r3, [r9]
	.loc 4 355 0
	ldrh	r6, [r4, #74]
.LVL1090:
	.loc 4 354 0
	strh	r3, [r10, #2]	@ movhi
	.loc 4 355 0
	ldrh	r3, [r9]
	.loc 4 357 0
	ldr	r1, [r8]
	ldr	r0, [r4, #3232]
	.loc 4 355 0
	mla	r6, r7, r6, r3
.LVL1091:
	.loc 4 356 0
	lsl	r3, r6, #10
	str	r3, [r4, #3228]
	.loc 4 357 0
	ldr	r3, .L1294+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
.LVL1092:
	.loc 4 358 0
	mov	r2, #1
	ldr	r0, .L1294+4
	mov	r1, r2
	bl	FlashEraseBlocks
.LVL1093:
	.loc 4 359 0
	mov	r3, #1
	ldr	r0, .L1294+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1094:
	.loc 4 360 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	bne	.L1277
	.loc 4 362 0
	uxth	r0, r6
	bl	FtlBbmMapBadBlock
.LVL1095:
	.loc 4 363 0
	b	.L1278
.L1279:
	.loc 4 347 0
	ldr	r3, [fp, #-48]
	b	.L1293
.L1283:
	.loc 4 383 0 discriminator 3
	bl	FtlBbmMapBadBlock
.LVL1096:
	b	.L1282
.LVL1097:
.L1286:
	.loc 4 396 0
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	.loc 4 398 0
	strheq	r5, [r4, #128]	@ movhi
	beq	.L1285
.L1287:
	.loc 4 402 0
	strh	r5, [r4, #132]	@ movhi
.L1288:
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
.LVL1098:
	.loc 4 418 0
	ldrh	r0, [r4, #128]
	bl	FtlBbmMapBadBlock
.LVL1099:
	.loc 4 419 0
	ldrh	r0, [r4, #132]
	bl	FtlBbmMapBadBlock
.LVL1100:
	.loc 4 421 0
	bl	FtlBbmTblFlush
.LVL1101:
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
.LVL1102:
	.loc 4 432 0
	mov	r0, r5
	sub	sp, fp, #40
.LCFI350:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1103:
.L1295:
	.align	2
.L1294:
	.word	.LANCHOR0+156
	.word	.LANCHOR0+3224
	.word	-3872
	.word	.LANCHOR0+3860
.LFE171:
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
.LFB203:
	.loc 2 786 0
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1104:
	.movsp ip
	mov	ip, sp
.LCFI351:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI352:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI353:
	.pad #108
	sub	sp, sp, #108
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 786 0
	.loc 2 794 0
	ldr	r4, .L1305
	.loc 2 786 0
	mov	r8, r0
	str	r2, [fp, #-136]
	.loc 2 794 0
	add	r2, r0, r1
.LVL1105:
	.loc 2 786 0
	mov	r6, r1
	.loc 2 794 0
	ldrh	r3, [r4, #72]
	cmp	r2, r3
	.loc 2 795 0
	mvnhi	r9, #0
	.loc 2 794 0
	bhi	.L1296
	.loc 2 791 0
	ldrh	r7, [r4, #84]
	mov	r9, #0
	lsr	r7, r0, r7
.LVL1106:
	lsl	r10, r7, #2
.LVL1107:
.L1298:
	.loc 2 797 0
	cmp	r6, #0
	bne	.L1303
.L1296:
	.loc 2 820 0
	mov	r0, r9
	sub	sp, fp, #40
.LCFI354:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1108:
.L1303:
.LCFI355:
	.loc 2 798 0
	ldr	r3, [r4, #3844]
.LVL1109:
	.loc 2 799 0
	mov	r0, r8
	.loc 2 798 0
	ldr	r2, [r3, r10]
.LVL1110:
	.loc 2 799 0
	ldrh	r3, [r4, #34]
	.loc 2 798 0
	str	r2, [fp, #-148]
	.loc 2 799 0
	mov	r1, r3
	str	r3, [fp, #-144]
	bl	__aeabi_uidivmod
.LVL1111:
	.loc 2 800 0
	ldr	r3, [fp, #-144]
	.loc 2 801 0
	ldr	r2, [fp, #-148]
.LVL1112:
	.loc 2 799 0
	str	r1, [fp, #-140]
	.loc 2 800 0
	sub	r5, r3, r1
.LVL1113:
	uxth	r5, r5
.LVL1114:
	.loc 2 801 0
	cmp	r6, r5
	.loc 2 802 0
	uxthcc	r5, r6
	.loc 2 803 0
	cmp	r2, #0
	cmpne	r5, r3
	movne	r1, #1
	moveq	r1, #0
	beq	.L1300
	.loc 2 805 0
	ldr	r3, [r4, #3808]
	.loc 2 807 0
	sub	r0, fp, #128
	.loc 2 804 0
	str	r2, [fp, #-124]
	.loc 2 807 0
	mov	r2, #1
.LVL1115:
	mov	r1, r2
	.loc 2 805 0
	str	r3, [fp, #-120]
	.loc 2 806 0
	sub	r3, fp, #108
	str	r3, [fp, #-116]
	.loc 2 807 0
	bl	FlashReadPages
.LVL1116:
.L1301:
	.loc 2 811 0
	lsl	r3, r5, #9
	ldr	r0, [r4, #3808]
	ldr	r1, [fp, #-136]
	.loc 2 815 0
	sub	r6, r6, r5
	.loc 2 811 0
	mov	r2, r3
	str	r3, [fp, #-144]
	ldr	r3, [fp, #-140]
	.loc 2 816 0
	add	r8, r8, r5
	add	r10, r10, #4
	.loc 2 811 0
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
.LVL1117:
	.loc 2 812 0
	ldr	r2, [r4, #3808]
	mov	r1, r7
	ldr	r0, .L1305+4
	.loc 2 814 0
	add	r7, r7, #1
	.loc 2 812 0
	bl	FtlMapWritePage
.LVL1118:
	.loc 2 817 0
	ldr	r2, [fp, #-136]
.LVL1119:
	.loc 2 813 0
	cmn	r0, #1
	.loc 2 817 0
	ldr	r3, [fp, #-144]
	.loc 2 813 0
	mvneq	r9, #0
	.loc 2 817 0
	add	r3, r2, r3
.LVL1120:
	str	r3, [fp, #-136]
	b	.L1298
.LVL1121:
.L1300:
	.loc 2 809 0
	ldrh	r2, [r4, #86]
.LVL1122:
	ldr	r0, [r4, #3808]
	bl	ftl_memset
.LVL1123:
	b	.L1301
.L1306:
	.align	2
.L1305:
	.word	.LANCHOR0
	.word	.LANCHOR0+3908
.LFE203:
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
.LFB226:
	.loc 2 2110 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI356:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI357:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI358:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2110 0
	.loc 2 2111 0
	ldr	r3, .L1309
	ldr	r2, .L1309+4
	ldr	r1, [r3, #2616]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
.LBB476:
.LBB477:
	.loc 2 2112 0
	ldr	r2, .L1309+8
	.loc 2 2125 0
	mov	r1, #1
	mov	r0, #0
	.loc 2 2112 0
	str	r2, [r3, #2620]
	.loc 2 2113 0
	ldr	r2, [r3, #2464]
	str	r2, [r3, #2704]
	.loc 2 2114 0
	ldr	r2, [r3, #2468]
	str	r2, [r3, #2708]
	.loc 2 2115 0
	ldr	r2, [r3, #2460]
	str	r2, [r3, #2624]
	.loc 2 2116 0
	ldr	r2, [r3, #2448]
	str	r2, [r3, #2628]
	.loc 2 2117 0
	ldr	r2, [r3, #2440]
	str	r2, [r3, #2632]
	.loc 2 2118 0
	ldr	r2, [r3, #2456]
	str	r2, [r3, #2636]
	.loc 2 2119 0
	ldr	r2, [r3, #2484]
	str	r2, [r3, #2644]
	.loc 2 2120 0
	ldr	r2, [r3, #2492]
	str	r2, [r3, #2648]
	.loc 2 2121 0
	ldr	r2, [r3, #2444]
	str	r2, [r3, #2652]
	.loc 2 2122 0
	ldr	r2, [r3, #2452]
	str	r2, [r3, #2656]
	.loc 2 2123 0
	ldr	r2, [r3, #2496]
	str	r2, [r3, #2660]
	.loc 2 2124 0
	ldr	r2, [r3, #2500]
	str	r2, [r3, #2664]
	.loc 2 2125 0
	ldr	r2, .L1309+12
	bl	FtlVendorPartWrite
.LVL1124:
	ldmfd	sp, {fp, sp, pc}
.L1310:
	.align	2
.L1309:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177344
	.word	.LANCHOR0+2616
.LBE477:
.LBE476:
.LFE226:
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
.LFB205:
	.loc 2 861 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1125:
	.movsp ip
	mov	ip, sp
.LCFI359:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI360:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI361:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 861 0
	.loc 2 864 0
	ldr	r2, .L1316
	ldrh	r3, [r2]
	cmp	r3, #31
	.loc 2 865 0
	addls	r3, r3, #1
	.loc 2 862 0
	movhi	r2, #32
.LVL1126:
	.loc 2 865 0
	strhls	r3, [r2]	@ movhi
.LVL1127:
	.loc 2 866 0
	movls	r2, #1
.LVL1128:
	.loc 2 868 0
	cmp	r0, #0
	ldr	r3, .L1316+4
	bne	.L1313
	.loc 2 868 0 is_stmt 0 discriminator 1
	ldr	r1, [r3, #3820]
	ldr	r0, [r1, #20]
.LVL1129:
	ldr	r1, [r1, #16]
	add	r2, r2, r0
.LVL1130:
	cmp	r1, r2
	bcc	.L1314
.LVL1131:
.L1313:
	.loc 2 870 0 is_stmt 1
	ldr	r2, [r3, #3820]
.LVL1132:
	.loc 2 875 0
	mov	r0, #64
.LVL1133:
	.loc 2 870 0
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	.loc 2 871 0
	ldr	r1, .L1316+8
	str	r1, [r2]
	.loc 2 872 0
	movw	r1, #3812
	ldrh	r1, [r3, r1]
	ldr	r2, [r3, #3820]
	lsl	r3, r1, #9
	str	r3, [r2, #12]
	.loc 2 873 0
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	.loc 2 874 0
	mov	r3, #0
	str	r3, [r2, #4]
	.loc 2 875 0
	bl	FtlVendorPartWrite
.LVL1134:
	.loc 2 876 0
	bl	Ftl_save_ext_data
.LVL1135:
.L1314:
	.loc 2 878 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1317:
	.align	2
.L1316:
	.word	.LANCHOR3-4080
	.word	.LANCHOR0
	.word	1112818501
.LFE205:
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
.LFB204:
	.loc 2 823 0
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1136:
	.movsp ip
	mov	ip, sp
.LCFI362:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI363:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI364:
	.pad #108
	sub	sp, sp, #108
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 823 0
	.loc 2 832 0
	ldr	r5, .L1327
	.loc 2 823 0
	mov	r9, r2
	.loc 2 832 0
	add	r2, r0, r1
.LVL1137:
	.loc 2 823 0
	mov	r7, r0
	mov	r6, r1
	.loc 2 832 0
	ldrh	r3, [r5, #72]
	cmp	r2, r3
	.loc 2 833 0
	mvnhi	r8, #0
	.loc 2 832 0
	bhi	.L1318
	.loc 2 828 0
	ldrh	r3, [r5, #84]
	mov	r8, #0
	lsr	r3, r0, r3
.LVL1138:
	lsl	r3, r3, #2
.LVL1139:
	str	r3, [fp, #-136]
.LVL1140:
.L1320:
	.loc 2 834 0
	cmp	r6, #0
	bne	.L1325
.L1318:
	.loc 2 857 0
	mov	r0, r8
	sub	sp, fp, #40
.LCFI365:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1141:
.L1325:
.LCFI366:
	.loc 2 836 0
	ldrh	r4, [r5, #34]
.LVL1142:
	mov	r0, r7
	.loc 2 835 0
	ldr	r3, [r5, #3844]
	ldr	r2, [fp, #-136]
	.loc 2 836 0
	mov	r1, r4
	.loc 2 835 0
	ldr	r10, [r3, r2]
	.loc 2 836 0
	bl	__aeabi_uidivmod
.LVL1143:
	.loc 2 837 0
	sub	r4, r4, r1
	str	r1, [fp, #-144]
	uxth	r4, r4
.LVL1144:
	.loc 2 838 0
	cmp	r6, r4
	.loc 2 839 0
	uxthcc	r4, r6
	.loc 2 840 0
	cmp	r10, #0
	lsl	r3, r4, #9
	str	r3, [fp, #-140]
	beq	.L1322
	.loc 2 842 0
	ldr	r2, [r5, #3808]
	.loc 2 843 0
	sub	r3, fp, #108
	.loc 2 844 0
	sub	r0, fp, #128
	.loc 2 843 0
	str	r3, [fp, #-116]
	.loc 2 841 0
	str	r10, [fp, #-124]
	.loc 2 842 0
	str	r2, [fp, #-120]
	.loc 2 844 0
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.LVL1145:
	.loc 2 847 0
	ldr	r1, [r5, #3808]
	mov	r0, r9
	ldr	r3, [fp, #-144]
	.loc 2 845 0
	ldr	r2, [fp, #-128]
	.loc 2 847 0
	add	r1, r1, r3, lsl #9
	.loc 2 846 0
	cmn	r2, #1
	.loc 2 847 0
	lsl	r2, r4, #9
	.loc 2 846 0
	mvneq	r8, #0
	.loc 2 847 0
	bl	ftl_memcpy
.LVL1146:
.L1324:
	.loc 2 854 0
	ldr	r3, [fp, #-140]
	.loc 2 852 0
	sub	r6, r6, r4
	.loc 2 853 0
	add	r7, r7, r4
	.loc 2 854 0
	add	r9, r9, r3
	ldr	r3, [fp, #-136]
	add	r3, r3, #4
	str	r3, [fp, #-136]
	b	.L1320
.L1322:
	.loc 2 849 0
	lsl	r2, r4, #9
	mov	r1, r10
	mov	r0, r9
	bl	ftl_memset
.LVL1147:
	b	.L1324
.L1328:
	.align	2
.L1327:
	.word	.LANCHOR0
.LFE204:
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
.LFB211:
	.loc 2 1334 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI367:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI368:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI369:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1334 0
	.loc 2 1335 0
	movw	r5, #3812
	mov	r0, #64
	ldr	r4, .L1331
	ldr	r2, [r4, #3820]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
.LVL1148:
	.loc 2 1336 0
	ldr	r3, [r4, #3820]
	ldr	r2, [r3]
	ldr	r3, .L1331+4
	cmp	r2, r3
	beq	.L1330
.LBB480:
.LBB481:
	.loc 2 1338 0
	ldr	r1, .L1331+8
	ldr	r0, .L1331+12
	bl	printk
.LVL1149:
	.loc 2 1339 0
	ldrh	r2, [r4, r5]
	mov	r1, #0
	ldr	r0, [r4, #3820]
	lsl	r2, r2, #9
	bl	ftl_memset
.LVL1150:
.L1330:
.LBE481:
.LBE480:
	.loc 2 1344 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1332:
	.align	2
.L1331:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC96
	.word	.LC71
.LFE211:
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
.LFB227:
	.loc 2 2130 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI370:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI371:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI372:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2130 0
	.loc 2 2131 0
	mov	r1, #1
	mov	r0, #0
	ldr	r4, .L1336
	ldr	r2, .L1336+4
	bl	FtlVendorPartRead
.LVL1151:
	.loc 2 2132 0
	ldr	r5, .L1336+8
	ldr	r3, [r4, #2616]
	cmp	r3, r5
	beq	.L1334
.LBB487:
.LBB488:
.LBB489:
	.loc 1 202 0
	mov	r1, #512
	ldr	r0, .L1336+4
	bl	__memzero
.LVL1152:
.LBE489:
.LBE488:
.LBE487:
	.loc 2 2134 0
	str	r5, [r4, #2616]
.L1334:
	.loc 2 2137 0
	ldr	r3, [r4, #2616]
	cmp	r3, r5
	bne	.L1335
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
.L1335:
	.loc 2 2152 0
	ldrh	r2, [r4, #68]
	ldr	r3, [r4, #2484]
	ldr	r0, [r4, #2480]
	ldrh	r1, [r4, #16]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
.LVL1153:
	str	r0, [r4, #2488]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1337:
	.align	2
.L1336:
	.word	.LANCHOR0
	.word	.LANCHOR0+2616
	.word	1179929683
.LFE227:
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlVpcTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
.LFB209:
	.loc 2 1004 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI373:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI374:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI375:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1004 0
	.loc 2 1016 0
	mov	r6, #0
	.loc 2 1047 0
	mov	r1, #255
	.loc 2 1010 0
	ldr	r4, .L1350
	.loc 2 1013 0
	ldr	r5, .L1350+4
	.loc 2 1075 0
	ldr	r10, .L1350+8
	.loc 2 1010 0
	ldr	r3, [r4, #2244]
	.loc 2 1011 0
	ldr	r7, [r4, #3244]
	.loc 2 1021 0
	sub	r8, r5, #52
	.loc 2 1056 0
	add	r9, r5, #668
	.loc 2 1010 0
	str	r3, [r4, #3232]
	.loc 2 1013 0
	movw	r3, #2556
	ldrh	r3, [r4, r3]
	.loc 2 1011 0
	str	r7, [r4, #3236]
	.loc 2 1017 0
	str	r6, [r7, #12]
	.loc 2 1013 0
	strh	r3, [r7, #2]	@ movhi
	.loc 2 1014 0
	ldr	r3, .L1350+12
	strh	r3, [r7]	@ movhi
	.loc 2 1015 0
	ldr	r3, [r4, #2564]
	.loc 2 1016 0
	str	r6, [r7, #8]
	.loc 2 1015 0
	str	r3, [r7, #4]
	.loc 2 1019 0
	ldr	r3, .L1350+16
	str	r3, [r4, #2504]
	.loc 2 1020 0
	ldr	r3, .L1350+20
	str	r3, [r4, #2508]
	.loc 2 1021 0
	ldrh	r3, [r5, #6]
	strh	r3, [r5, #-44]	@ movhi
	.loc 2 1022 0
	ldrh	r3, [r4, #30]
	strb	r3, [r4, #2514]
	.loc 2 1024 0
	movw	r3, #2280
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #-38]	@ movhi
	.loc 2 1025 0
	sub	r3, r5, #276
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #2286]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	.loc 2 1029 0
	ldrh	r2, [r5, #-226]
	.loc 2 1025 0
	strh	r3, [r5, #-36]	@ movhi
	.loc 2 1026 0
	ldrb	r3, [r4, #2288]	@ zero_extendqisi2
	strb	r3, [r4, #2515]
	.loc 2 1028 0
	movw	r3, #2328
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #-34]	@ movhi
	.loc 2 1029 0
	ldrb	r3, [r4, #2334]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #-32]	@ movhi
	.loc 2 1030 0
	ldrb	r3, [r4, #2336]	@ zero_extendqisi2
	strb	r3, [r4, #2516]
	.loc 2 1032 0
	movw	r3, #2376
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #-30]	@ movhi
	.loc 2 1033 0
	ldrh	r2, [r5, #-178]
	ldrb	r3, [r4, #2382]	@ zero_extendqisi2
	.loc 2 1047 0
	ldr	r0, [r4, #3232]
	.loc 2 1033 0
	orr	r3, r3, r2, lsl #6
	.loc 2 1047 0
	ldrh	r2, [r4, #86]
	.loc 2 1033 0
	strh	r3, [r5, #-28]	@ movhi
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
.LVL1154:
	.loc 2 1048 0
	mov	r1, r8
	mov	r2, #48
	ldr	r0, [r4, #3232]
	.loc 2 1005 0
	movw	r8, #65535
.LVL1155:
	.loc 2 1048 0
	bl	ftl_memcpy
.LVL1156:
	.loc 2 1049 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #3232]
	ldr	r1, [r4, #2260]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
.LVL1157:
	.loc 2 1050 0
	ldrh	r0, [r4, #16]
	ldr	r3, [r4, #3232]
	ldr	r1, [r4]
	lsr	r2, r0, #3
	add	r0, r0, #24
	lsl	r0, r0, #1
	add	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r3, r0
	bl	ftl_memcpy
.LVL1158:
	.loc 2 1051 0
	mov	r0, r6
	bl	FtlUpdateVaildLpn
.LVL1159:
.L1339:
	.loc 2 1053 0
	ldr	r3, [r4, #2244]
	.loc 2 1056 0
	mov	r0, r9
	.loc 2 1055 0
	ldrh	r2, [r5]
	.loc 2 1053 0
	str	r3, [r4, #3232]
	.loc 2 1054 0
	ldr	r3, [r4, #3244]
	str	r3, [r4, #3236]
	.loc 2 1055 0
	ldrh	r3, [r5, #2]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3228]
	.loc 2 1056 0
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.LVL1160:
	.loc 2 1057 0
	ldrh	r3, [r4, #80]
	ldrh	r2, [r5, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1340
	.loc 2 1059 0
	ldrh	r3, [r5]
	.loc 2 1058 0
	ldrh	r8, [r5, #4]
	.loc 2 1059 0
	strh	r3, [r5, #4]	@ movhi
	.loc 2 1060 0
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	.loc 2 1061 0
	bl	FtlFreeSysBlkQueueOut
.LVL1161:
	.loc 2 1066 0
	ldr	r3, [r4, #2472]
	.loc 2 1061 0
	strh	r0, [r5]	@ movhi
	.loc 2 1066 0
	add	r2, r3, #1
	str	r3, [r4, #2564]
	str	r2, [r4, #2472]
	.loc 2 1067 0
	lsl	r2, r0, #10
	str	r2, [r4, #3228]
	.loc 2 1068 0
	str	r3, [r7, #4]
	.loc 2 1070 0
	mov	r3, #1
	.loc 2 1069 0
	strh	r0, [r7, #2]	@ movhi
	.loc 2 1070 0
	mov	r2, r3
	mov	r1, r3
	mov	r0, r9
	bl	FlashProgPages
.LVL1162:
.L1340:
	.loc 2 1072 0
	ldrh	r3, [r5, #2]
	.loc 2 1073 0
	ldr	r2, [r4, #3224]
	.loc 2 1072 0
	add	r3, r3, #1
	uxth	r3, r3
	.loc 2 1073 0
	cmn	r2, #1
	.loc 2 1072 0
	strh	r3, [r5, #2]	@ movhi
	.loc 2 1073 0
	bne	.L1341
	.loc 2 1075 0
	cmp	r3, #1
	bne	.L1342
	.loc 2 1075 0 is_stmt 0 discriminator 1
	movw	r2, #1075
	mov	r1, r10
	ldr	r0, .L1350+24
	bl	printk
.LVL1163:
.L1342:
	.loc 2 1076 0 is_stmt 1
	ldrh	r3, [r5, #2]
	.loc 2 1078 0
	add	r6, r6, #1
.LVL1164:
	uxth	r6, r6
.LVL1165:
	.loc 2 1076 0
	cmp	r3, #1
	.loc 2 1077 0
	ldrheq	r3, [r4, #80]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	.loc 2 1079 0
	cmp	r6, #3
	bls	.L1339
	.loc 2 1080 0
	mov	r2, r6
	ldr	r1, [r4, #3228]
	ldr	r0, .L1350+28
	bl	printk
.LVL1166:
.L1345:
	b	.L1345
.L1341:
	.loc 2 1086 0
	cmp	r3, #1
	beq	.L1339
	.loc 2 1089 0
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1346
	.loc 2 1090 0
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.LVL1167:
.L1346:
	.loc 2 1092 0
	mov	r0, #0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1351:
	.align	2
.L1350:
	.word	.LANCHOR0
	.word	.LANCHOR0+2556
	.word	.LANCHOR1+454
	.word	-3932
	.word	1179929683
	.word	1342177344
	.word	.LC1
	.word	.LC97
.LFE209:
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
.LFB229:
	.loc 2 2192 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI376:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI377:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI378:
	.pad #8
	sub	sp, sp, #8
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2192 0
	.loc 2 2198 0
	ldr	r4, .L1364
	.loc 2 2199 0
	mov	r5, #0
.LVL1168:
	.loc 2 2197 0
	ldr	r1, .L1364+4
	ldr	r0, .L1364+8
	bl	printk
.LVL1169:
	.loc 2 2198 0
	ldrh	r2, [r4, #18]
	mov	r1, #0
	ldr	r0, [r4, #3824]
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL1170:
.L1353:
	.loc 2 2199 0 discriminator 1
	ldr	r3, [r4, #2436]
	cmp	r5, r3
	bcc	.L1355
	.loc 2 2209 0 discriminator 1
	ldr	r8, .L1364+12
	mov	r6, #0
	.loc 2 2210 0 discriminator 1
	ldr	r9, .L1364+16
.LVL1171:
.L1356:
	.loc 2 2206 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r5, r6
.LVL1172:
	cmp	r3, r5
	bhi	.L1360
	.loc 2 2218 0
	sub	sp, fp, #36
.LCFI379:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL1173:
.L1355:
.LCFI380:
	.loc 2 2200 0
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r5
	bl	log2phys
.LVL1174:
	.loc 2 2201 0
	ldr	r0, [fp, #-40]
	cmn	r0, #1
	beq	.L1354
	.loc 2 2202 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1175:
	.loc 2 2203 0
	ldr	r2, [r4, #3824]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1354:
	.loc 2 2199 0 discriminator 2
	add	r5, r5, #1
	b	.L1353
.LVL1176:
.L1360:
	.loc 2 2207 0
	uxth	r1, r6
	ldr	r3, [r4, #2260]
	movw	r0, #65535
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #3824]
	ldrh	r3, [r3, r7]
	cmp	r2, r0
	cmpne	r2, r3
	beq	.L1358
	.loc 2 2209 0
	mov	r0, r8
	bl	printk
.LVL1177:
	.loc 2 2210 0
	ldrh	r3, [r9]
	cmp	r3, r5
	beq	.L1358
	.loc 2 2210 0 is_stmt 0 discriminator 1
	ldr	r3, .L1364+20
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L1358
	.loc 2 2210 0 discriminator 2
	ldrh	r3, [r3, #-48]
	cmp	r3, r5
	beq	.L1358
	.loc 2 2211 0 is_stmt 1
	ldr	r3, [r4, #3824]
	.loc 2 2212 0
	mov	r0, r5
	.loc 2 2211 0
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #2260]
	strh	r2, [r3, r7]	@ movhi
	.loc 2 2212 0
	bl	update_vpc_list
.LVL1178:
	.loc 2 2213 0
	bl	l2p_flush
.LVL1179:
	.loc 2 2214 0
	bl	FtlVpcTblFlush
.LVL1180:
.L1358:
	add	r6, r6, #1
	b	.L1356
.L1365:
	.align	2
.L1364:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC87
	.word	.LC98
	.word	.LANCHOR0+2280
	.word	.LANCHOR0+2376
.LFE229:
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
.LFB239:
	.loc 2 2631 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1181:
	.movsp ip
	mov	ip, sp
.LCFI381:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI382:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI383:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2631 0
	mov	r7, r0
	.loc 2 2633 0
	ldr	r5, .L1376
	.loc 2 2632 0
	ldrh	r4, [r0]
.LVL1182:
	.loc 2 2633 0
	ldrh	r3, [r5, #16]
	cmp	r3, r4
	bcs	.L1367
	.loc 2 2633 0 is_stmt 0 discriminator 1
	movw	r2, #2633
	ldr	r1, .L1376+4
	ldr	r0, .L1376+8
	bl	printk
.LVL1183:
.L1367:
	.loc 2 2635 0 is_stmt 1
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1368
	.loc 2 2636 0
	ldr	r2, [r5, #2260]
	lsl	r3, r4, #1
	.loc 2 2637 0
	mov	r0, r4
	.loc 2 2636 0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1369
	.loc 2 2637 0
	bl	INSERT_DATA_LIST
.LVL1184:
.L1368:
	.loc 2 2641 0
	mov	r3, #1
	.loc 2 2642 0
	movw	r2, #65535
	.loc 2 2641 0
	strb	r3, [r7, #8]
	.loc 2 2642 0
	ldr	r3, .L1376+12
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r2
	beq	.L1370
	.loc 2 2643 0
	cmp	r4, r0
	bne	.L1371
	.loc 2 2643 0 is_stmt 0 discriminator 1
	ldr	r2, [r5, #2260]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1372
.L1371:
	.loc 2 2644 0 is_stmt 1
	bl	update_vpc_list
.LVL1185:
.L1372:
	.loc 2 2645 0
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L1370:
	.loc 2 2647 0
	mov	r0, r7
	bl	allocate_data_superblock
.LVL1186:
	.loc 2 2648 0
	bl	l2p_flush
.LVL1187:
	.loc 2 2649 0
	mov	r0, #0
	bl	FtlEctTblFlush
.LVL1188:
	.loc 2 2650 0
	bl	FtlVpcTblFlush
.LVL1189:
	.loc 2 2652 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1369:
	.loc 2 2639 0
	bl	INSERT_FREE_LIST
.LVL1190:
	b	.L1368
.L1377:
	.align	2
.L1376:
	.word	.LANCHOR0
	.word	.LANCHOR1+490
	.word	.LC1
	.word	.LANCHOR0+3952
.LFE239:
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
.LFB155:
	.loc 3 902 0
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1191:
	.movsp ip
	mov	ip, sp
.LCFI384:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI385:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI386:
	.pad #20
	sub	sp, sp, #20
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 902 0
	ldr	r6, .L1392
	.loc 3 911 0
	mov	r7, #0
.LVL1192:
	.loc 3 931 0
	ldr	r10, .L1392+4
	.loc 3 902 0
	mov	r4, r3
	.loc 3 909 0
	mov	r2, #0
.LVL1193:
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
.LVL1194:
	.loc 3 902 0
	mov	r5, r0
	mov	r9, r1
	.loc 3 909 0
	bl	FlashProgPages
.LVL1195:
.L1379:
	.loc 3 911 0 discriminator 1
	cmp	r7, r9
	beq	.L1386
	.loc 3 917 0
	mov	r8, #0
	b	.L1387
.LVL1196:
.L1382:
	.loc 3 913 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1197:
	.loc 3 914 0
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1380
	.loc 3 915 0
	ldr	r1, [r6, #2260]
	lsl	r3, r3, #1
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	.loc 3 916 0
	ldrh	r3, [r6, #78]
	.loc 3 917 0
	strb	r8, [r4, #6]
	.loc 3 918 0
	strh	r8, [r4, #4]	@ movhi
	.loc 3 916 0
	strh	r3, [r4, #2]	@ movhi
.L1380:
	.loc 3 920 0
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1381
	.loc 3 921 0
	mov	r0, r4
	bl	allocate_new_data_superblock
.LVL1198:
.L1381:
	.loc 3 924 0
	ldr	r3, [r6, #2712]
	add	r3, r3, #1
	str	r3, [r6, #2712]
	.loc 3 925 0
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
.LVL1199:
	.loc 3 926 0
	mov	r0, r4
	bl	get_new_active_ppa
.LVL1200:
	.loc 3 928 0
	mov	r2, #0
	.loc 3 927 0
	str	r0, [r5, #4]
	.loc 3 926 0
	str	r0, [fp, #-48]
	.loc 3 928 0
	mov	r1, #1
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r0, r5
	bl	FlashProgPages
.LVL1201:
.L1387:
	.loc 3 912 0
	ldr	r3, [r5]
.LVL1202:
	cmn	r3, #1
	beq	.L1382
	.loc 3 931 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
	cmp	r2, r3
	bcc	.L1383
	.loc 3 931 0 is_stmt 0 discriminator 1
	movw	r2, #931
	mov	r1, r10
	ldr	r0, .L1392+8
	bl	printk
.LVL1203:
.L1383:
	.loc 3 932 0 is_stmt 1
	ldr	r3, [r5, #4]
	sub	r1, fp, #44
	.loc 3 933 0
	mov	r2, #1
	ldr	r0, [r5, #16]
	.loc 3 932 0
	str	r3, [r1, #-4]!
.LVL1204:
	.loc 3 933 0
	bl	log2phys
.LVL1205:
	.loc 3 935 0
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #12]
.LVL1206:
	.loc 3 937 0
	ubfx	r0, r3, #10, #16
	str	r3, [fp, #-56]
	bl	P2V_block_in_plane
.LVL1207:
	.loc 3 938 0
	ldr	r3, [fp, #-56]
.LVL1208:
	.loc 3 937 0
	mov	r8, r0
	.loc 3 938 0
	cmn	r3, #1
	beq	.L1384
	.loc 3 940 0
	ldr	r2, [r6, #2260]
	lsl	r3, r0, #1
.LVL1209:
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1385
	.loc 3 941 0
	mov	r1, r0
	ldr	r0, .L1392+12
	bl	printk
.LVL1210:
.L1385:
	.loc 3 942 0
	mov	r0, r8
	bl	decrement_vpc_count
.LVL1211:
.L1384:
	.loc 3 911 0 discriminator 2
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L1379
.LVL1212:
.L1386:
	.loc 3 946 0
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #8]
.LVL1213:
	cmp	r2, r3
	bcc	.L1378
	.loc 3 946 0 is_stmt 0 discriminator 1
	movw	r2, #946
	ldr	r1, .L1392+4
	ldr	r0, .L1392+8
	bl	printk
.LVL1214:
.L1378:
	.loc 3 947 0 is_stmt 1
	sub	sp, fp, #40
.LCFI387:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1215:
.L1393:
	.align	2
.L1392:
	.word	.LANCHOR0
	.word	.LANCHOR1+519
	.word	.LC1
	.word	.LC99
.LFE155:
	.fnend
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlCacheWriteBack
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
.LFB156:
	.loc 3 950 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI388:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI389:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI390:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 950 0
	.loc 3 951 0
	ldr	r4, .L1399
	ldr	r0, [r4, #3948]
	cmp	r0, #0
	beq	.L1395
	.loc 3 952 0
	ldr	r3, .L1399+4
	mov	r2, #0
	mov	r1, #1
	ldr	r3, [r3, #-4076]
	bl	FtlProgPages
.LVL1216:
	.loc 3 953 0
	mov	r3, #0
	str	r3, [r4, #3948]
.L1395:
	.loc 3 956 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1400:
	.align	2
.L1399:
	.word	.LANCHOR0
	.word	.LANCHOR3
.LFE156:
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
.LFB152:
	.loc 3 797 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI391:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI392:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI393:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 797 0
	.loc 3 798 0
	bl	FtlCacheWriteBack
.LVL1217:
	.loc 3 799 0
	bl	l2p_flush
.LVL1218:
	.loc 3 800 0
	mov	r0, #1
	bl	FtlEctTblFlush
.LVL1219:
	.loc 3 801 0
	bl	FtlVpcTblFlush
.LVL1220:
	.loc 3 803 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.LFE152:
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
	.fnstart
.LFB153:
	.loc 3 806 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI394:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI395:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI396:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 806 0
	.loc 3 807 0
	ldr	r3, .L1404
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1403
	.loc 3 808 0
	bl	FtlSysFlush
.LVL1221:
.L1403:
	.loc 3 810 0
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1405:
	.align	2
.L1404:
	.word	.LANCHOR2
.LFE153:
	.fnend
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	FtlDiscard
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
.LFB158:
	.loc 3 1154 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1222:
	.movsp ip
	mov	ip, sp
.LCFI397:
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, fp, ip, lr}
.LCFI398:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI399:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 1154 0
	.loc 3 1161 0
	ldr	r6, .L1421
	add	r2, r0, r1
	.loc 3 1154 0
	mov	r7, r0
	mov	r5, r1
	.loc 3 1161 0
	ldr	r3, [r6, #116]
	cmp	r2, r3
	.loc 3 1162 0
	mvnhi	r0, #0
.LVL1223:
	.loc 3 1161 0
	bhi	.L1406
	.loc 3 1164 0
	cmp	r1, #31
	bhi	.L1408
.LVL1224:
.L1413:
	.loc 3 1165 0
	mov	r0, #0
.L1406:
	.loc 3 1198 0
	sub	sp, fp, #32
.LCFI400:
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.LVL1225:
.L1408:
.LCFI401:
	.loc 3 1167 0
	bl	FtlCacheWriteBack
.LVL1226:
	.loc 3 1169 0
	ldrh	r4, [r6, #34]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidiv
.LVL1227:
	.loc 3 1170 0
	smulbb	r3, r0, r4
	.loc 3 1169 0
	mov	r8, r0
.LVL1228:
	.loc 3 1170 0
	sub	r7, r7, r3
.LVL1229:
	uxth	r7, r7
.LVL1230:
	.loc 3 1171 0
	cmp	r7, #0
	beq	.L1409
	.loc 3 1172 0
	sub	r4, r4, r7
	.loc 3 1173 0
	add	r8, r0, #1
	.loc 3 1172 0
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	.loc 3 1174 0
	sub	r5, r5, r4
.L1409:
	.loc 3 1181 0
	ldr	r4, .L1421+4
	.loc 3 1177 0
	mvn	r3, #0
	str	r3, [fp, #-40]
.LVL1231:
.L1410:
	.loc 3 1178 0
	ldrh	r3, [r6, #34]
	cmp	r5, r3
	bcs	.L1412
	.loc 3 1191 0
	ldr	r3, .L1421+4
	ldr	r2, [r3, #-4072]
	cmp	r2, #32
	bls	.L1413
	.loc 3 1193 0
	mov	r4, #0
	str	r4, [r3, #-4072]
	.loc 3 1194 0
	bl	l2p_flush
.LVL1232:
	.loc 3 1195 0
	bl	FtlVpcTblFlush
.LVL1233:
	b	.L1413
.L1412:
	.loc 3 1179 0
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r8
	bl	log2phys
.LVL1234:
	.loc 3 1180 0
	ldr	r3, [fp, #-44]
	cmn	r3, #1
	beq	.L1411
	.loc 3 1181 0
	ldr	r3, [r4, #-4072]
	.loc 3 1183 0
	mov	r2, #1
	sub	r1, fp, #40
	mov	r0, r8
	.loc 3 1181 0
	add	r3, r3, #1
	str	r3, [r4, #-4072]
	.loc 3 1182 0
	ldr	r3, [r6, #2444]
	add	r3, r3, #1
	str	r3, [r6, #2444]
	.loc 3 1183 0
	bl	log2phys
.LVL1235:
	.loc 3 1184 0
	ldr	r0, [fp, #-44]
.LVL1236:
	ubfx	r0, r0, #10, #16
.LVL1237:
	bl	P2V_block_in_plane
.LVL1238:
	.loc 3 1185 0
	bl	decrement_vpc_count
.LVL1239:
.L1411:
	.loc 3 1188 0
	ldrh	r3, [r6, #34]
	.loc 3 1187 0
	add	r8, r8, #1
	.loc 3 1188 0
	sub	r5, r5, r3
	b	.L1410
.L1422:
	.align	2
.L1421:
	.word	.LANCHOR0
	.word	.LANCHOR3
.LFE158:
	.fnend
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
.LFB248:
	.loc 5 112 0
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1240:
	.movsp ip
	mov	ip, sp
.LCFI402:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI403:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI404:
	.pad #12
	sub	sp, sp, #12
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 112 0
	.loc 5 115 0
	ldr	r4, .L1449
	.loc 5 119 0
	movw	r3, #2376
	movw	r2, #65535
	ldrh	r5, [r4, r3]
	.loc 5 115 0
	ldrh	r1, [r4, #78]
.LVL1241:
	.loc 5 119 0
	cmp	r5, r2
	bne	.L1424
.LVL1242:
.L1434:
	.loc 5 152 0
	mov	r3, #0
	str	r3, [r4, #3960]
	.loc 5 153 0
	movw	r3, #2376
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1447
.L1425:
	.loc 5 154 0
	mov	r0, #0
.LVL1243:
.L1423:
	.loc 5 199 0
	sub	sp, fp, #40
.LCFI405:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1244:
.L1424:
.LCFI406:
	.loc 5 121 0
	cmp	r0, #0
	beq	.L1427
	.loc 5 123 0
	ldr	r3, .L1449+4
	ldrh	r0, [r3, #4]
.LVL1245:
	cmp	r0, r2
	beq	.L1428
.L1429:
	.loc 5 122 0
	mov	r1, #2
.LVL1246:
.L1427:
	.loc 5 131 0
	ldr	r0, .L1449+8
.LVL1247:
	bl	FtlGcScanTempBlk
.LVL1248:
	.loc 5 132 0
	cmn	r0, #1
	.loc 5 131 0
	str	r0, [fp, #-48]
.LVL1249:
	.loc 5 132 0
	beq	.L1430
	.loc 5 135 0
	ldr	r2, [r4, #2248]
	lsl	r5, r5, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L1431
	.loc 5 136 0
	sub	r3, r3, #5
	.loc 5 137 0
	mov	r0, #1
	.loc 5 136 0
	strh	r3, [r2, r5]	@ movhi
	.loc 5 137 0
	bl	FtlEctTblFlush
.LVL1250:
.L1431:
	.loc 5 139 0
	ldr	r3, [r4, #3960]
	cmp	r3, #0
	bne	.L1432
	.loc 5 140 0
	ldr	r3, [r4, #2712]
	.loc 5 141 0
	ldr	r0, [fp, #-48]
	.loc 5 140 0
	add	r3, r3, #1
	.loc 5 141 0
	ubfx	r0, r0, #10, #16
	.loc 5 140 0
	str	r3, [r4, #2712]
	.loc 5 141 0
	bl	FtlBbmMapBadBlock
.LVL1251:
	.loc 5 142 0
	bl	FtlBbmTblFlush
.LVL1252:
.L1432:
	.loc 5 144 0
	mov	r3, #0
	str	r3, [r4, #3960]
.L1443:
	.loc 5 149 0
	mov	r0, #1
	b	.L1423
.LVL1253:
.L1428:
	.loc 5 124 0
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	.loc 5 125 0
	movw	r3, #2276
	ldrh	r3, [r4, r3]
	cmp	r3, #17
	bhi	.L1429
	b	.L1427
.LVL1254:
.L1430:
	.loc 5 148 0
	ldr	r3, .L1449+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1443
	b	.L1434
.LVL1255:
.L1447:
	.loc 5 160 0
	bl	FtlCacheWriteBack
.LVL1256:
	.loc 5 162 0
	movw	r3, #3180
	ldrb	r1, [r4, #2383]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	ldrh	r3, [r4, #78]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1435
	.loc 5 162 0 is_stmt 0 discriminator 1
	mov	r2, #162
	ldr	r1, .L1449+12
	ldr	r0, .L1449+16
	bl	printk
.LVL1257:
.L1435:
	.loc 5 163 0 is_stmt 1
	ldrb	r2, [r4, #2383]	@ zero_extendqisi2
	movw	r3, #2376
	ldrh	r0, [r4, #78]
	.loc 5 165 0
	mov	r6, #0
	.loc 5 163 0
	ldrh	r3, [r4, r3]
	ldr	r1, [r4, #2260]
	.loc 5 165 0
	ldr	r9, .L1449+20
	.loc 5 163 0
	smulbb	r2, r2, r0
	.loc 5 168 0
	ldr	r10, .L1449+12
	.loc 5 163 0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	.loc 5 164 0
	movw	r3, #3180
	ldr	r2, [r4, #2460]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2460]
.LVL1258:
.L1436:
	.loc 5 165 0 discriminator 1
	ldrh	r2, [r9]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1440
	.loc 5 181 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1259:
	.loc 5 182 0
	movw	r3, #2376
	ldr	r1, [r4, #2260]
	ldrh	r0, [r4, r3]
	mov	r5, r3
.LVL1260:
	lsl	r2, r0, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1441
	.loc 5 183 0
	bl	INSERT_DATA_LIST
.LVL1261:
.L1442:
	.loc 5 186 0
	mvn	r6, #0
	.loc 5 187 0
	movw	r3, #3180
	.loc 5 186 0
	strh	r6, [r4, r5]	@ movhi
	.loc 5 187 0
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	.loc 5 188 0
	movw	r3, #3172
	strh	r5, [r4, r3]	@ movhi
	.loc 5 189 0
	bl	l2p_flush
.LVL1262:
	.loc 5 190 0
	bl	FtlVpcTblFlush
.LVL1263:
	.loc 5 191 0
	movw	r3, #2276
	ldrh	r2, [r4, r3]
	movw	r3, #2552
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	.loc 5 192 0
	movwgt	r3, #2568
	.loc 5 193 0
	movgt	r2, #20
	.loc 5 192 0
	strhgt	r6, [r4, r3]	@ movhi
	.loc 5 193 0
	movwgt	r3, #3128
	strhgt	r2, [r4, r3]	@ movhi
	b	.L1425
.LVL1264:
.L1440:
	.loc 5 166 0
	uxth	r3, r6
	mov	r7, #12
	ldr	r8, [r4, #3176]
	mul	r7, r7, r3
	.loc 5 168 0
	ldr	r3, [r4, #2436]
	.loc 5 166 0
	add	r5, r8, r7
	.loc 5 168 0
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1437
	.loc 5 168 0 is_stmt 0 discriminator 1
	mov	r2, #168
	mov	r1, r10
	ldr	r0, .L1449+16
	bl	printk
.LVL1265:
.L1437:
	.loc 5 169 0 is_stmt 1
	mov	r2, #0
	sub	r1, fp, #48
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1266:
	.loc 5 170 0
	ldr	r3, [fp, #-48]
	ldr	r0, [r8, r7]
	cmp	r0, r3
	bne	.L1438
	.loc 5 171 0
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1267:
	.loc 5 172 0
	mov	r2, #1
	.loc 5 171 0
	mov	r7, r0
	.loc 5 172 0
	add	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
.LVL1268:
	.loc 5 173 0
	mov	r0, r7
.L1448:
	.loc 5 177 0
	bl	decrement_vpc_count
.LVL1269:
.L1439:
	add	r6, r6, #1
	b	.L1436
.L1438:
	.loc 5 175 0
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1439
	.loc 5 177 0
	ldr	r3, .L1449+8
	ldrh	r0, [r3]
	b	.L1448
.LVL1270:
.L1441:
	.loc 5 185 0
	bl	INSERT_FREE_LIST
.LVL1271:
	b	.L1442
.L1450:
	.align	2
.L1449:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2376
	.word	.LANCHOR1+532
	.word	.LC1
	.word	.LANCHOR0+3180
.LFE248:
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
.LFB251:
	.loc 5 289 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI407:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI408:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI409:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 289 0
	.loc 5 290 0
	ldr	r4, .L1453
	ldr	r5, .L1453+4
	ldrh	r1, [r4, #78]
	mov	r0, r5
	bl	FtlGcScanTempBlk
.LVL1272:
	.loc 5 291 0
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #78]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
.LBB492:
.LBB493:
	.loc 5 293 0
	add	r0, r5, #1488
	bl	FtlMapBlkWriteDump_data
.LVL1273:
	.loc 5 294 0
	mov	r0, #0
	bl	FtlGcFreeTempBlock
.LVL1274:
	.loc 5 295 0
	mov	r3, #0
	str	r3, [r4, #3960]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1454:
	.align	2
.L1453:
	.word	.LANCHOR0
	.word	.LANCHOR0+2376
.LBE493:
.LBE492:
.LFE251:
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
.LFB223:
	.loc 2 2032 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI410:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI411:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI412:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2032 0
	.loc 2 2033 0
	mov	r6, #0
	ldr	r4, .L1456
	.loc 2 2035 0
	add	r5, r4, #2272
	.loc 2 2033 0
	str	r6, [r4, #3972]
	.loc 2 2035 0
	add	r5, r5, #8
	.loc 2 2037 0
	add	r4, r4, #2320
	.loc 2 2035 0
	mov	r0, r5
	.loc 2 2037 0
	add	r4, r4, #8
	.loc 2 2035 0
	bl	FtlRecoverySuperblock
.LVL1275:
	.loc 2 2036 0
	mov	r0, r5
	bl	FtlSlcSuperblockCheck
.LVL1276:
	.loc 2 2037 0
	mov	r0, r4
	bl	FtlRecoverySuperblock
.LVL1277:
	.loc 2 2038 0
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
.LVL1278:
	.loc 2 2039 0
	bl	FtlGcPageRecovery
.LVL1279:
	.loc 2 2040 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1280:
	.loc 2 2042 0
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1457:
	.align	2
.L1456:
	.word	.LANCHOR0
.LFE223:
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
.LFB253:
	.loc 5 316 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI413:
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, fp, ip, lr}
.LCFI414:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI415:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 316 0
	.loc 5 320 0
	mov	r3, #0
	.loc 5 321 0
	mov	r5, #0
	.loc 5 320 0
	ldr	r4, .L1465
	.loc 5 322 0
	mov	r6, #20
	.loc 5 320 0
	mov	r2, r3
	ldr	r1, [r4, #3144]
	ldr	r0, [r4, #3796]
	bl	FlashProgPages
.LVL1281:
.L1459:
	.loc 5 321 0 discriminator 1
	ldr	r1, [r4, #3144]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1462
	.loc 5 335 0
	ldr	r0, [r4, #3796]
	bl	FtlGcBufFree
.LVL1282:
	.loc 5 337 0
	ldr	r3, .L1465+4
	.loc 5 336 0
	mov	r0, #0
	str	r0, [r4, #3144]
	.loc 5 337 0
	ldrh	r3, [r3, #4]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, r7, fp, sp, pc}
	.loc 5 338 0
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1283:
	b	.L1464
.L1462:
	.loc 5 322 0
	mul	r3, r6, r3
	ldr	r2, [r4, #3796]
	add	r5, r5, #1
	ldr	r0, [r2, r3]
	add	r1, r2, r3
	cmn	r0, #1
	bne	.L1460
	.loc 5 323 0
	movw	r1, #2376
	ldr	ip, [r4, #2260]
	ldrh	r2, [r4, r1]
	mov	lr, #0
	lsl	r2, r2, #1
	strh	lr, [ip, r2]	@ movhi
	.loc 5 325 0
	ldr	r2, [r4, #2712]
	.loc 5 324 0
	strh	r0, [r4, r1]	@ movhi
	.loc 5 325 0
	add	r2, r2, #1
	str	r2, [r4, #2712]
	.loc 5 326 0
	ldr	r2, [r4, #3796]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.LVL1284:
	.loc 5 327 0
	bl	FtlBbmTblFlush
.LVL1285:
	.loc 5 328 0
	bl	FtlGcPageVarInit
.LVL1286:
.L1464:
	.loc 5 339 0
	mov	r0, #1
	.loc 5 342 0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1460:
	.loc 5 331 0 discriminator 2
	ldr	r3, [r1, #12]
.LVL1287:
	.loc 5 332 0 discriminator 2
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
.LVL1288:
	b	.L1459
.L1466:
	.align	2
.L1465:
	.word	.LANCHOR0
	.word	.LANCHOR0+2376
.LFE253:
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
.LFB252:
	.loc 5 300 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI416:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI417:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI418:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 300 0
	.loc 5 301 0
	movw	r3, #2376
	ldr	r4, .L1470
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1468
	.loc 5 301 0 is_stmt 0 discriminator 1
	ldr	r3, .L1470+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1469
.L1468:
	.loc 5 302 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1289:
	.loc 5 303 0
	mov	r0, #0
	.loc 5 304 0
	mov	r5, #0
	.loc 5 303 0
	bl	FtlGcFreeTempBlock
.LVL1290:
	.loc 5 305 0
	ldr	r0, .L1470+4
	.loc 5 304 0
	strb	r5, [r4, #2384]
	.loc 5 305 0
	bl	allocate_data_superblock
.LVL1291:
	.loc 5 306 0
	movw	r3, #3172
	strh	r5, [r4, r3]	@ movhi
	.loc 5 307 0
	movw	r3, #3180
	strh	r5, [r4, r3]	@ movhi
	.loc 5 308 0
	bl	l2p_flush
.LVL1292:
	.loc 5 309 0
	mov	r0, r5
	bl	FtlEctTblFlush
.LVL1293:
	.loc 5 310 0
	bl	FtlVpcTblFlush
.LVL1294:
.L1469:
	.loc 5 312 0
	ldr	r0, .L1470+4
	bl	get_new_active_ppa
.LVL1295:
	.loc 5 313 0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1471:
	.align	2
.L1470:
	.word	.LANCHOR0
	.word	.LANCHOR0+2376
.LFE252:
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
.LFB259:
	.loc 5 470 0
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1296:
	.movsp ip
	mov	ip, sp
.LCFI419:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI420:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI421:
	.pad #44
	sub	sp, sp, #44
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 470 0
	.loc 5 478 0
	ldr	r3, .L1584
	.loc 5 470 0
	mov	r5, r0
	.loc 5 478 0
	ldr	r0, [r3, #3780]
.LVL1297:
	mov	r6, r3
	cmp	r0, #0
	.loc 5 479 0
	movne	r0, #0
	.loc 5 478 0
	bne	.L1472
	.loc 5 481 0
	movw	r2, #2268
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	bls	.L1472
	.loc 5 485 0
	ldr	r2, .L1584+4
	ldrh	r1, [r2, #4]
.LVL1298:
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1474
.L1477:
.LBB497:
.LBB498:
	.loc 5 489 0
	ldr	r3, .L1584+8
	movw	r0, #65535
	ldrh	r1, [r3]
	cmp	r1, r0
	bne	.L1475
.L1476:
	.loc 5 496 0
	ldr	r3, [r6, #3132]
	.loc 5 497 0
	movw	r2, #2568
	ldrh	r2, [r6, r2]
	movw	r4, #65535
	.loc 5 496 0
	add	r3, r3, #1
	add	r3, r3, r5, lsl #7
	.loc 5 497 0
	cmp	r2, r4
	.loc 5 496 0
	str	r3, [r6, #3132]
	.loc 5 497 0
	bne	.L1478
	movw	r1, #2376
	ldrh	r4, [r6, r1]
	cmp	r4, r2
	.loc 5 473 0
	movne	r4, r2
	.loc 5 497 0
	bne	.L1478
	movw	r2, #3182
	ldrh	r7, [r6, r2]
	cmp	r7, r4
	bne	.L1478
	.loc 5 498 0
	movw	r10, #2276
	ldrh	r2, [r6, r10]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	.loc 5 473 0
	movls	r4, r7
	.loc 5 498 0
	bls	.L1478
.LBB499:
	.loc 5 502 0
	ldr	r3, .L1584+12
	.loc 5 501 0
	mov	r9, #0
	str	r9, [r6, #3132]
	.loc 5 502 0
	strh	r9, [r3, #-4]	@ movhi
	.loc 5 503 0
	bl	GetSwlReplaceBlock
.LVL1299:
	.loc 5 504 0
	cmp	r0, r7
	.loc 5 503 0
	mov	r4, r0
	.loc 5 504 0
	movne	r7, r0
	bne	.L1480
	.loc 5 505 0
	movw	r8, #3130
	ldrh	r2, [r6, r10]
	ldrh	r3, [r6, r8]
	cmp	r2, r3
	bcs	.L1481
	.loc 5 506 0
	mov	r0, #64
	bl	List_get_gc_head_node
.LVL1300:
	.loc 5 507 0
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1483
	.loc 5 508 0
	mov	r0, r3
	ldr	r3, [r6, #2260]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1484
	.loc 5 509 0
	mov	r0, r9
	bl	List_get_gc_head_node
.LVL1301:
	uxth	r7, r0
	.loc 5 510 0
	mov	r3, #128
	strh	r3, [r6, r8]	@ movhi
	.loc 5 520 0
	cmp	r7, r4
	beq	.L1483
.L1480:
	.loc 5 521 0
	movw	r0, #3128
	ldr	r3, [r6, #2260]
	ldrh	r0, [r6, r0]
	lsl	r1, r7, #1
	movw	r2, #2276
	ldrh	r3, [r3, r1]
	mov	r4, r7
	ldrh	r2, [r6, r2]
	str	r0, [sp, #4]
	ldr	r0, [r6, #2248]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1584+16
	str	r1, [sp]
	mov	r1, r7
	bl	printk
.LVL1302:
	b	.L1483
.L1474:
.LBE499:
.LBE498:
.LBE497:
	.loc 5 485 0 discriminator 1
	movw	r1, #2376
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1477
	.loc 5 486 0
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.LVL1303:
	cmp	r0, #0
	beq	.L1477
	.loc 5 487 0
	mov	r0, #1
	b	.L1472
.L1475:
.LBB504:
.LBB502:
	.loc 5 489 0
	movw	r2, #3182
	ldrh	ip, [r6, r2]
	cmp	ip, r0
	.loc 5 490 0
	strheq	r1, [r6, r2]	@ movhi
	.loc 5 491 0
	mvneq	r2, #0
	strheq	r2, [r3]	@ movhi
	b	.L1476
.L1484:
.LBB500:
	.loc 5 512 0
	mov	r3, #64
.L1577:
	.loc 5 516 0
	strh	r3, [r6, r8]	@ movhi
.L1483:
	.loc 5 523 0
	bl	FtlGcReFreshBadBlk
.LVL1304:
.L1478:
.LBE500:
	.loc 5 527 0
	movw	r2, #65535
	sub	r3, r4, r2
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r5, #0
	movne	r5, #0
	andeq	r5, r3, #1
.LVL1305:
	cmp	r5, #0
	beq	.L1486
	.loc 5 528 0
	movw	r3, #2276
	ldrh	r3, [r6, r3]
	cmp	r3, #24
	.loc 5 529 0
	movhi	r5, #1
.LVL1306:
	.loc 5 528 0
	bhi	.L1487
	ldrh	r5, [r6, #78]
.LVL1307:
	.loc 5 530 0
	cmp	r3, #16
	.loc 5 531 0
	lsrhi	r5, r5, #5
.LVL1308:
	.loc 5 530 0
	bhi	.L1487
	.loc 5 532 0
	cmp	r3, #12
	.loc 5 533 0
	lsrhi	r5, r5, #4
.LVL1309:
	.loc 5 532 0
	bhi	.L1487
	.loc 5 534 0
	cmp	r3, #8
	.loc 5 535 0
	lsrhi	r5, r5, #2
.LVL1310:
.L1487:
	.loc 5 539 0
	movw	r2, #3128
	ldrh	r1, [r6, r2]
	cmp	r1, r3
	bcs	.L1491
	.loc 5 540 0
	movw	r3, #2376
	movw	r1, #65535
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L1492
	movw	r1, #3182
	ldrh	r1, [r6, r1]
	cmp	r1, r3
	bne	.L1492
	.loc 5 542 0
	ldr	r3, .L1584+12
	ldrh	r0, [r3, #-4]
	cmp	r0, #0
	bne	.L1493
	ldr	r3, [r6, #2436]
	ldr	r1, [r6, #2432]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	.loc 5 545 0
	movcs	r3, #18
	.loc 5 542 0
	bcs	.L1578
.L1493:
	.loc 5 543 0
	movw	r3, #2552
	movw	r2, #3128
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1578:
	.loc 5 545 0
	strh	r3, [r6, r2]	@ movhi
	.loc 5 546 0
	mov	r3, #0
	str	r3, [r6, #3140]
.LVL1311:
.L1472:
.LBE502:
.LBE504:
	.loc 5 758 0
	sub	sp, fp, #40
.LCFI422:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1312:
.L1481:
.LCFI423:
.LBB505:
.LBB503:
.LBB501:
	.loc 5 516 0
	mov	r3, #80
	b	.L1577
.LVL1313:
.L1492:
.LBE501:
	.loc 5 549 0
	movw	r3, #2552
	movw	r2, #3128
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r6, r2]	@ movhi
.L1491:
	.loc 5 551 0
	movw	r3, #3186
	movw	r4, #65535
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	.loc 5 552 0
	addne	r5, r5, #32
.LVL1314:
	uxthne	r5, r5
.LVL1315:
.L1497:
	.loc 5 597 0
	movw	r3, #2568
	ldrh	r2, [r6, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1506
	.loc 5 598 0
	cmp	r4, r2
	.loc 5 599 0
	strhne	r4, [r6, r3]	@ movhi
	.loc 5 598 0
	bne	.L1508
	.loc 5 600 0
	movw	r2, #3182
	ldrh	r3, [r6, r2]
	cmp	r3, r4
	beq	.L1508
	.loc 5 604 0
	ldr	r1, [r6, #2260]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	.loc 5 605 0
	mvneq	r3, #0
	strheq	r3, [r6, r2]	@ movhi
	.loc 5 606 0
	movw	r3, #3182
	ldrh	r1, [r6, r3]
	movw	r2, #2568
	strh	r1, [r6, r2]	@ movhi
	.loc 5 607 0
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1508:
	.loc 5 611 0
	movw	r7, #2568
	.loc 5 610 0
	mov	r3, #0
	.loc 5 611 0
	ldrh	r0, [r6, r7]
	.loc 5 610 0
	strb	r3, [r6, #2576]
	.loc 5 611 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1506
	.loc 5 612 0
	bl	IsBlkInGcList
.LVL1316:
	cmp	r0, #0
	.loc 5 615 0
	ldr	r8, .L1584+20
	.loc 5 613 0
	mvnne	r3, #0
	strhne	r3, [r6, r7]	@ movhi
	.loc 5 615 0
	movw	r7, #2568
	ldrh	r2, [r6, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1506
	.loc 5 616 0
	mov	r0, r8
	bl	make_superblock
.LVL1317:
	.loc 5 619 0
	ldr	r2, .L1584+12
	.loc 5 617 0
	mov	r3, #0
	strh	r3, [r8, #2]	@ movhi
	.loc 5 618 0
	strb	r3, [r6, #2574]
	.loc 5 619 0
	strh	r3, [r2, #-2]	@ movhi
	.loc 5 620 0
	ldrh	r3, [r6, r7]
	ldr	r1, [r6, #2260]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	strh	r3, [r2]	@ movhi
.L1506:
	.loc 5 625 0
	movw	r3, #2568
	movw	r2, #2280
	ldrh	r3, [r6, r3]
	ldrh	r2, [r6, r2]
	cmp	r2, r3
	beq	.L1512
	movw	r2, #2328
	ldrh	r2, [r6, r2]
	cmp	r2, r3
	.loc 5 631 0
	ldrne	r10, .L1584
	movne	r6, r10
	.loc 5 625 0
	bne	.L1513
.L1512:
	.loc 5 626 0
	mvn	r2, #0
	movw	r3, #2568
	strh	r2, [r6, r3]	@ movhi
.LVL1318:
.L1583:
	.loc 5 652 0
	ldr	r3, .L1584+12
	ldrh	r0, [r3, #-4]
	b	.L1472
.LVL1319:
.L1486:
	.loc 5 557 0
	movw	r1, #2376
	ldrh	r7, [r6, r1]
	cmp	r7, r2
	bne	.L1555
	movw	r2, #3182
	ldrh	r2, [r6, r2]
	cmp	r2, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1555
	movw	r3, #2568
	ldrh	r3, [r6, r3]
	cmp	r3, r7
	beq	.L1498
.L1503:
	.loc 5 556 0
	mov	r4, r7
.L1555:
	mov	r5, #1
.LVL1320:
	b	.L1497
.LVL1321:
.L1498:
	.loc 5 560 0
	movw	r3, #2276
	movw	r2, #3128
	ldrh	r8, [r6, r3]
	ldrh	r3, [r6, r2]
	.loc 5 558 0
	str	r5, [r6, #3140]
	ldr	r4, .L1584+12
	.loc 5 560 0
	cmp	r3, r8
	bcs	.L1499
	.loc 5 561 0
	ldrh	r3, [r4, #-4]
	cmp	r3, #0
	bne	.L1500
	ldr	r3, [r6, #2436]
	ldr	r1, [r6, #2432]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	.loc 5 564 0
	movcs	r3, #18
	.loc 5 561 0
	bcs	.L1579
.L1500:
	.loc 5 562 0
	movw	r3, #2552
	movw	r2, #3128
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1579:
	.loc 5 564 0
	strh	r3, [r6, r2]	@ movhi
.L1582:
	.loc 5 578 0
	bl	FtlReadRefresh
.LVL1322:
	.loc 5 579 0
	ldrh	r0, [r4, #-4]
	b	.L1472
.L1499:
	.loc 5 572 0
	ldrh	r0, [r4, #-4]
	cmp	r0, #0
	bne	.L1503
	.loc 5 573 0
	movw	r3, #2552
	ldrh	r5, [r6, r3]
	add	r3, r5, r5, lsl #1
	asr	r3, r3, #2
	strh	r3, [r6, r2]	@ movhi
	.loc 5 574 0
	bl	List_get_gc_head_node
.LVL1323:
	.loc 5 575 0
	uxth	r0, r0
	ldr	r3, [r6, #2260]
	ldrh	r1, [r6, #80]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldrh	r3, [r6, #8]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1504
	.loc 5 576 0
	sub	r5, r5, #1
	.loc 5 575 0
	cmp	r8, r5
	bge	.L1582
.L1504:
	.loc 5 584 0
	cmp	r2, #0
	bne	.L1503
	.loc 5 585 0
	movw	r0, #65535
	bl	decrement_vpc_count
.LVL1324:
	.loc 5 586 0
	movw	r3, #2276
	ldrh	r0, [r6, r3]
	add	r0, r0, #1
	b	.L1472
.LVL1325:
.L1516:
	str	r0, [fp, #-60]
	.loc 5 640 0
	mov	r0, r2
	str	r2, [fp, #-56]
	add	r9, r9, #1
	bl	IsBlkInGcList
.LVL1326:
	cmp	r0, #0
	ldr	r2, [fp, #-56]
	ldr	r3, [fp, #-60]
	.loc 5 641 0
	strhne	r9, [r8]	@ movhi
	.loc 5 640 0
	bne	.L1515
	.loc 5 645 0
	uxth	r3, r3
	ldrh	lr, [r6, #8]
	ldr	r0, [r6, #2260]
	.loc 5 644 0
	uxth	r9, r9
	.loc 5 645 0
	lsl	r1, r3, #1
	ldrh	r3, [r6, #78]
	.loc 5 644 0
	strh	r9, [r8]	@ movhi
	.loc 5 645 0
	ldrh	ip, [r0, r1]
	mul	r3, lr, r3
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1519
	.loc 5 646 0
	cmp	ip, #8
	cmphi	r9, #48
	bls	.L1520
	ldr	ip, .L1584+24
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1520
.L1519:
	.loc 5 647 0
	ldr	lr, .L1584+28
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L1520:
	.loc 5 649 0
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r4, r0
	moveq	r3, #1
	movne	r3, #0
	bne	.L1521
	.loc 5 650 0
	mvn	r2, #0
	movw	r3, #2568
	strh	r2, [r6, r3]	@ movhi
	.loc 5 651 0
	mov	r3, #0
	ldr	r2, .L1584+28
	strh	r3, [r2]	@ movhi
	b	.L1583
.L1521:
	.loc 5 655 0
	cmp	r1, #0
	bne	.L1522
	.loc 5 656 0
	bl	decrement_vpc_count
.LVL1327:
	.loc 5 657 0
	ldr	r3, .L1584+28
	ldr	r2, .L1584+28
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L1515
.L1522:
	.loc 5 660 0
	strb	r3, [r6, #2576]
	.loc 5 662 0
	ldr	r3, .L1584+32
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L1523
	movw	r2, #662
	ldr	r1, .L1584+36
	ldr	r0, .L1584+40
	bl	printk
.LVL1328:
.L1523:
	.loc 5 663 0
	ldr	r3, .L1584+44
	ldrh	r2, [r7]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1524
	movw	r2, #663
	ldr	r1, .L1584+36
	ldr	r0, .L1584+40
	bl	printk
.LVL1329:
.L1524:
	.loc 5 664 0
	ldr	r3, .L1584+48
	ldrh	r2, [r7]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1525
	mov	r2, #664
	ldr	r1, .L1584+36
	ldr	r0, .L1584+40
	bl	printk
.LVL1330:
.L1525:
	.loc 5 665 0
	ldr	r0, .L1584+20
	bl	make_superblock
.LVL1331:
	.loc 5 667 0
	ldrh	r2, [r7]
	.loc 5 666 0
	mov	r3, #0
	ldr	r1, .L1584+12
	.loc 5 667 0
	ldr	r0, [r6, #2260]
	lsl	r2, r2, #1
	.loc 5 666 0
	strh	r3, [r1, #-2]	@ movhi
	.loc 5 667 0
	ldrh	r2, [r0, r2]
	.loc 5 668 0
	strh	r3, [r7, #2]	@ movhi
	.loc 5 669 0
	strb	r3, [r6, #2574]
	.loc 5 667 0
	strh	r2, [r1]	@ movhi
.LVL1332:
.L1514:
	.loc 5 675 0
	bl	FtlReadRefresh
.LVL1333:
	.loc 5 678 0
	mov	r3, #1
	str	r3, [r6, #3780]
	.loc 5 679 0
	ldrh	r3, [r6, #78]
.LVL1334:
	str	r3, [fp, #-68]
	.loc 5 680 0
	ldrh	r3, [r7, #2]
.LVL1335:
	ldr	r1, [fp, #-68]
.LVL1336:
	add	r2, r3, r5
	cmp	r2, r1
	.loc 5 681 0
	movgt	r2, r1
	subgt	r5, r2, r3
.LVL1337:
	.loc 5 693 0
	mov	r3, #0
	.loc 5 681 0
	uxthgt	r5, r5
.LVL1338:
.L1581:
	str	r3, [fp, #-64]
	.loc 5 683 0
	ldrh	r3, [fp, #-64]
	ldr	r8, .L1584+20
.LVL1339:
	cmp	r5, r3
	bls	.L1535
	.loc 5 688 0
	ldrh	r7, [r8, #2]
	mov	r2, #0
.LVL1340:
	ldr	r3, [fp, #-64]
	mov	r9, #20
.LVL1341:
	.loc 5 685 0
	ldrh	ip, [r10, #8]
	.loc 5 688 0
	ldr	r0, [r10, #3160]
	add	r7, r7, r3
	str	r2, [fp, #-56]
	add	r3, r8, #14
	.loc 5 687 0
	movw	r8, #65535
	b	.L1536
.LVL1342:
.L1529:
	.loc 5 686 0
	ldrh	r1, [r3, #2]!
.LVL1343:
	.loc 5 687 0
	cmp	r1, r8
	beq	.L1528
	.loc 5 688 0
	ldr	lr, [fp, #-56]
.LVL1344:
	orr	r1, r7, r1, lsl #10
.LVL1345:
	mla	lr, r9, lr, r0
.LVL1346:
	str	r1, [lr, #4]
	.loc 5 689 0
	ldr	r1, [fp, #-56]
.LVL1347:
	add	r1, r1, #1
.LVL1348:
	uxth	r1, r1
.LVL1349:
	str	r1, [fp, #-56]
.LVL1350:
.L1528:
	add	r2, r2, #1
.LVL1351:
.L1536:
	.loc 5 685 0
	uxth	r1, r2
.LVL1352:
	cmp	ip, r1
	bhi	.L1529
	.loc 5 693 0
	ldrb	r2, [r10, #2576]	@ zero_extendqisi2
	ldr	r1, [fp, #-56]
	bl	FlashReadPages
.LVL1353:
	mov	r3, #0
.LVL1354:
.L1580:
	str	r3, [fp, #-60]
	.loc 5 695 0
	ldr	r2, [fp, #-56]
.LVL1355:
	ldrh	r3, [fp, #-60]
	cmp	r3, r2
	ldrcs	r3, [fp, #-64]
	addcs	r3, r3, #1
	bcs	.L1581
.L1534:
	ldr	r3, [fp, #-60]
	mov	r7, #20
	mul	r7, r7, r3
	.loc 5 696 0
	ldr	r3, [r10, #3160]
	add	r2, r3, r7
.LVL1356:
	.loc 5 697 0
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1531
	.loc 5 696 0
	ldr	r9, [r2, #12]
	.loc 5 697 0
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	bne	.L1531
	.loc 5 699 0
	ldr	r8, [r9, #8]
	.loc 5 700 0
	cmn	r8, #1
	bne	.L1532
	mov	r2, #700
	ldr	r1, .L1584+36
	ldr	r0, .L1584+40
	bl	printk
.LVL1357:
.L1532:
	.loc 5 701 0
	mov	r2, #0
	sub	r1, fp, #48
	mov	r0, r8
	bl	log2phys
.LVL1358:
	.loc 5 702 0
	ldr	r1, [r6, #3160]
	ldr	r3, [fp, #-48]
	add	r1, r1, r7
	ldr	r2, [r1, #4]
	cmp	r2, r3
	bne	.L1531
	.loc 5 704 0
	ldr	r8, .L1584+52
.LVL1359:
	.loc 5 705 0
	mov	r2, #20
	ldr	r0, [r6, #3144]
	ldr	r1, [r1, #16]
	.loc 5 704 0
	ldrh	r3, [r8]
	.loc 5 705 0
	str	r2, [fp, #-76]
	.loc 5 704 0
	add	r3, r3, #1
	strh	r3, [r8], #2	@ movhi
	.loc 5 705 0
	ldr	r3, [r6, #3796]
	mla	r3, r2, r0, r3
	str	r1, [r3, #16]
	str	r3, [fp, #-72]
	.loc 5 706 0
	bl	Ftl_get_new_temp_ppa
.LVL1360:
	ldr	r3, [fp, #-72]
	.loc 5 707 0
	ldr	r1, [r6, #3144]
	ldr	r2, [fp, #-76]
	.loc 5 706 0
	str	r0, [r3, #4]
	.loc 5 707 0
	ldr	r3, [r6, #3796]
	mla	r2, r2, r1, r3
	ldr	r3, [r6, #3160]
	add	r3, r3, r7
	ldr	r1, [r3, #8]
	str	r1, [r2, #8]
	.loc 5 716 0
	mov	r1, #1
	.loc 5 708 0
	ldr	r3, [r3, #12]
	str	r3, [r2, #12]
	.loc 5 709 0
	ldr	r3, [fp, #-48]
.LVL1361:
	str	r3, [r9, #12]
	.loc 5 710 0
	ldr	r3, .L1584+48
.LVL1362:
	ldrh	r2, [r3]
	str	r3, [fp, #-72]
	strh	r2, [r9, #2]	@ movhi
	.loc 5 713 0
	ldr	r2, [r6, #2476]
	.loc 5 716 0
	ldr	r0, [r6, #3160]
	.loc 5 713 0
	str	r2, [r9, #4]
	.loc 5 715 0
	ldr	r2, [r6, #3144]
	.loc 5 716 0
	add	r0, r0, r7
	.loc 5 715 0
	add	r2, r2, #1
	str	r2, [r6, #3144]
	.loc 5 716 0
	bl	FtlGcBufAlloc
.LVL1363:
	.loc 5 717 0
	ldrb	r1, [r6, #2383]	@ zero_extendqisi2
	ldr	r2, [r6, #3144]
	cmp	r1, r2
	beq	.L1533
	ldr	r3, [fp, #-72]
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1531
.L1533:
	.loc 5 718 0
	bl	Ftl_gc_temp_data_write_back
.LVL1364:
	cmp	r0, #0
	.loc 5 719 0
	ldrne	r3, .L1584
	movne	r2, #0
	.loc 5 720 0
	ldrhne	r0, [r8, #-4]
	.loc 5 719 0
	strne	r2, [r3, #3780]
	bne	.L1472
.LVL1365:
.L1531:
	ldr	r3, [fp, #-60]
	add	r3, r3, #1
	b	.L1580
.LVL1366:
.L1535:
	.loc 5 727 0
	ldrh	r3, [r8, #2]
	add	r5, r5, r3
.LVL1367:
	.loc 5 729 0
	ldr	r3, [fp, #-68]
.LVL1368:
	.loc 5 727 0
	uxth	r5, r5
	.loc 5 729 0
	cmp	r3, r5
	.loc 5 727 0
	strh	r5, [r8, #2]	@ movhi
	ldr	r5, .L1584+12
	.loc 5 729 0
	bls	.L1537
	ldrh	r2, [r5, #-2]
.LVL1369:
	ldrh	r3, [r5]
.LVL1370:
	cmp	r2, r3
	bne	.L1538
.LVL1371:
.L1537:
	.loc 5 731 0
	ldr	r3, [r10, #3144]
.LVL1372:
	cmp	r3, #0
	beq	.L1539
	.loc 5 732 0
	bl	Ftl_gc_temp_data_write_back
.LVL1373:
	cmp	r0, #0
	.loc 5 733 0
	movne	r3, #0
	.loc 5 734 0
	ldrhne	r0, [r5, #-4]
	.loc 5 733 0
	strne	r3, [r10, #3780]
	bne	.L1472
.LVL1374:
.L1539:
	.loc 5 737 0
	ldrh	r1, [r5, #-2]
	cmp	r1, #0
	bne	.L1540
	ldrh	r3, [r8]
	ldr	r2, [r10, #2260]
.LVL1375:
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	cmp	r0, #0
	beq	.L1540
	.loc 5 739 0
	strh	r1, [r2, r3]	@ movhi
	.loc 5 740 0
	ldrh	r0, [r8]
	bl	update_vpc_list
.LVL1376:
	.loc 5 741 0
	bl	FtlCacheWriteBack
.LVL1377:
	.loc 5 742 0
	bl	l2p_flush
.LVL1378:
	.loc 5 743 0
	bl	FtlVpcTblFlush
.LVL1379:
.L1540:
	.loc 5 745 0
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L1538:
	.loc 5 747 0
	ldr	r3, .L1584+56
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L1541
	.loc 5 749 0
	ldrh	r5, [r10, #78]
.LVL1380:
.L1513:
	.loc 5 631 0
	ldr	r7, .L1584+20
	movw	r3, #65535
	ldrh	r2, [r7]
.LVL1381:
	cmp	r2, r3
	bne	.L1514
	.loc 5 635 0
	add	r8, r7, #568
	.loc 5 632 0
	mov	r3, #0
	str	r3, [r10, #3140]
.L1515:
	.loc 5 635 0
	ldrh	r9, [r8]
.LVL1382:
	mov	r0, r9
	bl	List_get_gc_head_node
.LVL1383:
	ldr	r1, .L1584+20
	uxth	r2, r0
	strh	r2, [r1]	@ movhi
	.loc 5 636 0
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1516
	.loc 5 637 0
	mov	r3, #0
	.loc 5 638 0
	mov	r0, #8
	.loc 5 637 0
	strh	r3, [r8]	@ movhi
	b	.L1472
.LVL1384:
.L1541:
	.loc 5 754 0
	ldrh	r0, [r5, #-4]
	.loc 5 752 0
	mov	r2, #0
.LVL1385:
	str	r2, [r10, #3780]
	.loc 5 754 0
	cmp	r0, r2
	.loc 5 757 0
	addeq	r0, r3, #1
	b	.L1472
.L1585:
	.align	2
.L1584:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3184
	.word	.LANCHOR3-4064
	.word	.LC100
	.word	.LANCHOR0+2568
	.word	.LANCHOR0+3172
	.word	.LANCHOR0+3136
	.word	.LANCHOR0+2280
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2376
	.word	.LANCHOR3-4066
	.word	.LANCHOR0+2276
.LBE503:
.LBE505:
.LFE259:
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	sftl_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
	.fnstart
.LFB260:
	.loc 5 761 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI424:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI425:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI426:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 5 761 0
	.loc 5 762 0
	mov	r1, #1
	mov	r0, r1
	bl	rk_ftl_garbage_collect
.LVL1386:
	.loc 5 763 0
	ldmfd	sp, {fp, sp, pc}
.LFE260:
	.fnend
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	.fnstart
.LFB154:
	.loc 3 813 0
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1387:
	.movsp ip
	mov	ip, sp
.LCFI427:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI428:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI429:
	.pad #60
	sub	sp, sp, #60
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 813 0
	.loc 3 823 0
	cmp	r0, #16
	.loc 3 813 0
	mov	r7, r1
	str	r2, [fp, #-76]
	mov	r9, r3
	.loc 3 823 0
	bne	.L1588
	.loc 3 824 0
	mov	r2, r3
.LVL1388:
	ldr	r1, [fp, #-76]
	add	r0, r7, #256
.LVL1389:
	bl	FtlVendorPartRead
.LVL1390:
	str	r0, [fp, #-56]
.LVL1391:
.L1587:
	.loc 3 900 0
	ldr	r0, [fp, #-56]
.LVL1392:
	sub	sp, fp, #40
.LCFI430:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1393:
.L1588:
.LCFI431:
	.loc 3 829 0
	ldr	r3, [fp, #-76]
.LVL1394:
	ldr	r4, .L1621
	add	r3, r1, r3
.LVL1395:
	str	r3, [fp, #-64]
	ldr	r2, [fp, #-64]
	ldr	r3, [r4, #116]
	cmp	r2, r3
	bhi	.L1608
	.loc 3 829 0 is_stmt 0 discriminator 1
	ldr	r3, .L1621+4
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1609
	.loc 3 832 0 is_stmt 1
	bl	FtlCacheWriteBack
.LVL1396:
	.loc 3 834 0
	ldrh	r5, [r4, #34]
	mov	r0, r7
	.loc 3 827 0
	mov	r8, #0
.LVL1397:
	.loc 3 839 0
	mov	r6, r8
.LVL1398:
	.loc 3 834 0
	mov	r1, r5
	bl	__aeabi_uidiv
.LVL1399:
	.loc 3 835 0
	ldr	r3, [fp, #-64]
	mov	r1, r5
	.loc 3 834 0
	str	r0, [fp, #-68]
	.loc 3 835 0
	sub	r0, r3, #1
	bl	__aeabi_uidiv
.LVL1400:
	.loc 3 836 0
	ldr	r3, [fp, #-68]
.LVL1401:
	.loc 3 834 0
	ldr	r5, [fp, #-68]
.LVL1402:
	.loc 3 835 0
	str	r0, [fp, #-72]
	.loc 3 836 0
	rsb	r3, r3, #1
.LVL1403:
	.loc 3 827 0
	str	r8, [fp, #-84]
.LVL1404:
	.loc 3 836 0
	add	r3, r3, r0
.LVL1405:
	.loc 3 819 0
	str	r8, [fp, #-80]
	.loc 3 836 0
	str	r3, [fp, #-60]
	.loc 3 840 0
	ldr	r2, [fp, #-60]
.LVL1406:
	ldr	r3, [r4, #2440]
	.loc 3 814 0
	str	r8, [fp, #-56]
	.loc 3 840 0
	add	r3, r3, r2
	str	r3, [r4, #2440]
.LVL1407:
.L1590:
	.loc 3 841 0
	ldr	r3, [fp, #-60]
.LVL1408:
	cmp	r3, #0
	bne	.L1606
	.loc 3 897 0
	ldr	r3, .L1621
.LVL1409:
	movw	r2, #3186
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1607
	.loc 3 897 0 is_stmt 0 discriminator 1
	movw	r2, #2276
	ldrh	r3, [r3, r2]
	cmp	r3, #31
	bhi	.L1587
.L1607:
	.loc 3 898 0 is_stmt 1
	mov	r1, #1
	mov	r0, #0
.LVL1410:
	bl	rk_ftl_garbage_collect
.LVL1411:
	b	.L1587
.LVL1412:
.L1606:
	.loc 3 842 0
	mov	r2, #0
	sub	r1, fp, #48
	mov	r0, r5
.LVL1413:
	bl	log2phys
.LVL1414:
	.loc 3 843 0
	ldr	r2, [fp, #-48]
	cmn	r2, #1
	moveq	r10, #0
.LVL1415:
	beq	.L1592
	.loc 3 850 0
	ldr	r3, [r4, #3792]
	mov	r1, #20
	ldrh	r10, [r4, #34]
.LVL1416:
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	.loc 3 851 0
	ldr	r2, [fp, #-68]
.LVL1417:
	cmp	r5, r2
	bne	.L1596
	.loc 3 852 0
	ldr	r2, [r4, #3776]
.LVL1418:
	.loc 3 853 0
	mov	r1, r10
	mov	r0, r7
	.loc 3 852 0
	str	r3, [fp, #-88]
	str	r2, [r3, #8]
	.loc 3 853 0
	bl	__aeabi_uidivmod
.LVL1419:
	ldr	r3, [fp, #-76]
.LVL1420:
	.loc 3 854 0
	sub	r2, r10, r1
	.loc 3 853 0
	str	r1, [fp, #-80]
.LVL1421:
	cmp	r3, r2
	movcs	r3, r2
.LVL1422:
	.loc 3 856 0
	cmp	r3, r10
	str	r3, [fp, #-84]
	ldr	r3, [fp, #-88]
	.loc 3 857 0
	streq	r9, [r3, #8]
.LVL1423:
.L1597:
	.loc 3 866 0
	ldrh	r2, [r4, #88]
.LVL1424:
	ldr	r1, [r4, #3772]
	.loc 3 867 0
	str	r5, [r3, #16]
	.loc 3 866 0
	mul	r2, r6, r2
	.loc 3 868 0
	add	r6, r6, #1
	.loc 3 866 0
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
	b	.L1595
.LVL1425:
.L1594:
	.loc 3 845 0
	mla	r0, r0, r5, r10
.LVL1426:
	.loc 3 846 0
	ldr	r3, [fp, #-64]
	cmp	r3, r0
	movhi	r3, #1
	movls	r3, #0
	cmp	r7, r0
	movhi	r3, #0
	cmp	r3, #0
	beq	.L1593
	.loc 3 847 0
	sub	r0, r0, r7
.LVL1427:
.LBB512:
.LBB513:
.LBB514:
	.loc 1 202 0
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.LVL1428:
.L1593:
.LBE514:
.LBE513:
.LBE512:
	.loc 3 844 0 discriminator 2
	add	r10, r10, #1
.L1592:
	.loc 3 844 0 is_stmt 0 discriminator 1
	ldrh	r0, [r4, #34]
.LVL1429:
	cmp	r10, r0
	bcc	.L1594
.L1595:
	.loc 3 873 0 is_stmt 1
	ldr	r3, [fp, #-60]
.LVL1430:
	.loc 3 871 0
	add	r5, r5, #1
	.loc 3 873 0
	subs	r3, r3, #1
	str	r3, [fp, #-60]
	beq	.L1599
	.loc 3 873 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #8]
.LVL1431:
	cmp	r6, r3, lsl #2
	bne	.L1590
.LVL1432:
.L1599:
	.loc 3 874 0 is_stmt 1
	cmp	r6, #0
	beq	.L1590
	.loc 3 875 0
	mov	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3792]
	.loc 3 876 0
	mov	r10, #0
.LVL1433:
	.loc 3 875 0
	bl	FlashReadPages
.LVL1434:
	.loc 3 882 0
	lsl	r3, r8, #9
	str	r3, [fp, #-96]
	.loc 3 879 0
	ldr	r3, [fp, #-80]
.LVL1435:
	lsl	r3, r3, #9
.LVL1436:
	str	r3, [fp, #-88]
	ldr	r3, [fp, #-84]
	lsl	r3, r3, #9
	str	r3, [fp, #-92]
.L1605:
	mov	r3, #20
	.loc 3 877 0
	ldr	r2, [r4, #3792]
	mul	r3, r3, r10
	ldr	r0, [fp, #-68]
.LVL1437:
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1601
	.loc 3 878 0
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3776]
	cmp	r1, r2
	bne	.L1602
	str	r3, [fp, #-100]
	.loc 3 879 0
	mov	r0, r9
.LVL1438:
	ldr	r3, [fp, #-88]
	ldr	r2, [fp, #-92]
	add	r1, r1, r3
.LVL1439:
.L1620:
	.loc 3 882 0
	bl	ftl_memcpy
.LVL1440:
	ldr	r3, [fp, #-100]
.LVL1441:
.L1602:
	.loc 3 884 0
	ldr	r2, [r4, #3792]
	ldr	r0, [r2, r3]
.LVL1442:
	add	ip, r2, r3
	cmn	r0, #1
	.loc 3 885 0
	ldreq	r1, [r4, #2688]
	.loc 3 886 0
	streq	r0, [fp, #-56]
	.loc 3 885 0
	addeq	r1, r1, #1
	streq	r1, [r4, #2688]
	.loc 3 888 0
	ldr	r3, [r2, r3]
	cmp	r3, #256
	bne	.L1604
.LBB515:
	.loc 3 889 0
	ldr	r0, [ip, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
.LVL1443:
	.loc 3 890 0
	bl	FtlGcRefreshBlock
.LVL1444:
.L1604:
.LBE515:
	.loc 3 876 0 discriminator 2
	add	r10, r10, #1
	cmp	r6, r10
	bne	.L1605
	.loc 3 893 0
	mov	r6, #0
	b	.L1590
.LVL1445:
.L1596:
	.loc 3 858 0
	ldr	r2, [fp, #-72]
.LVL1446:
	cmp	r5, r2
	bne	.L1598
	.loc 3 859 0
	ldr	r2, [r4, #3768]
.LVL1447:
	.loc 3 860 0
	ldr	r1, [fp, #-64]
	.loc 3 859 0
	str	r2, [r3, #8]
	.loc 3 860 0
	mul	r2, r10, r5
	sub	r8, r1, r2
	.loc 3 861 0
	cmp	r10, r8
	.loc 3 862 0
	subeq	r2, r2, r7
	addeq	r2, r9, r2, lsl #9
	streq	r2, [r3, #8]
	b	.L1597
.LVL1448:
.L1598:
	.loc 3 864 0
	mul	r10, r5, r10
	sub	r10, r10, r7
	add	r10, r9, r10, lsl #9
	str	r10, [r3, #8]
	b	.L1597
.LVL1449:
.L1601:
	.loc 3 880 0
	ldr	r0, [fp, #-72]
.LVL1450:
	cmp	r0, r1
	bne	.L1602
	.loc 3 881 0
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3768]
	cmp	r1, r2
	bne	.L1602
	str	r3, [fp, #-100]
	.loc 3 882 0
	ldrh	r0, [r4, #34]
.LVL1451:
	ldr	r3, [fp, #-72]
.LVL1452:
	ldr	r2, [fp, #-96]
	mul	r0, r3, r0
	sub	r0, r0, r7
	add	r0, r9, r0, lsl #9
	b	.L1620
.LVL1453:
.L1608:
	.loc 3 830 0
	mvn	r3, #0
.L1609:
	str	r3, [fp, #-56]
	b	.L1587
.L1622:
	.align	2
.L1621:
	.word	.LANCHOR0
	.word	.LANCHOR2
.LFE154:
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_read, %function
sftl_read:
	.fnstart
.LFB159:
	.loc 3 1201 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1454:
	.movsp ip
	mov	ip, sp
.LCFI432:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI433:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI434:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 1201 0
	.loc 3 1202 0
	mov	r3, r2
	mov	r2, r1
.LVL1455:
	mov	r1, r0
.LVL1456:
	mov	r0, #0
.LVL1457:
	bl	FtlRead
.LVL1458:
	.loc 3 1204 0
	ldmfd	sp, {fp, sp, pc}
.LFE159:
	.fnend
	.size	sftl_read, .-sftl_read
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
.LFB157:
	.loc 3 959 0
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1459:
	.movsp ip
	mov	ip, sp
.LCFI435:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI436:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI437:
	.pad #68
	sub	sp, sp, #68
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 959 0
	.loc 3 967 0
	cmp	r0, #16
	.loc 3 959 0
	mov	r9, r1
	str	r2, [fp, #-80]
	str	r3, [fp, #-84]
	.loc 3 967 0
	bne	.L1625
	.loc 3 968 0
	mov	r2, r3
.LVL1460:
	ldr	r1, [fp, #-80]
	add	r0, r9, #256
.LVL1461:
	bl	FtlVendorPartWrite
.LVL1462:
.L1624:
	.loc 3 1150 0
	sub	sp, fp, #40
.LCFI438:
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.LVL1463:
.L1625:
.LCFI439:
	.loc 3 971 0
	ldr	r3, [fp, #-80]
.LVL1464:
	ldr	r10, .L1690
	add	r6, r1, r3
	ldr	r3, [r10, #116]
.LVL1465:
	cmp	r6, r3
	.loc 3 972 0
	mvnhi	r0, #0
.LVL1466:
	.loc 3 971 0
	bhi	.L1624
	.loc 3 971 0 is_stmt 0 discriminator 1
	ldr	r5, .L1690+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1624
	.loc 3 974 0 is_stmt 1
	ldr	r3, .L1690+8
	mov	r2, #2048
	.loc 3 975 0
	ldrh	r4, [r10, #34]
	mov	r0, r9
	.loc 3 974 0
	str	r2, [r3, #-4060]
	.loc 3 975 0
	mov	r1, r4
	bl	__aeabi_uidiv
.LVL1467:
	.loc 3 976 0
	mov	r1, r4
	.loc 3 975 0
	mov	r8, r0
.LVL1468:
	.loc 3 976 0
	sub	r0, r6, #1
	bl	__aeabi_uidiv
.LVL1469:
	.loc 3 977 0
	sub	r6, r0, r8
	.loc 3 980 0
	ldr	r7, [r10, #3948]
	.loc 3 977 0
	add	r3, r6, #1
.LVL1470:
	.loc 3 979 0
	add	r2, r6, #1
.LVL1471:
	.loc 3 977 0
	str	r3, [fp, #-76]
	.loc 3 979 0
	ldr	r3, [r10, #2448]
	.loc 3 980 0
	cmp	r7, #0
	.loc 3 976 0
	str	r0, [fp, #-92]
.LVL1472:
	.loc 3 979 0
	add	r3, r3, r2
	str	r3, [r10, #2448]
	.loc 3 980 0
	beq	.L1627
	.loc 3 981 0
	ldr	r3, [r7, #16]
	cmp	r8, r3
	beq	.L1628
	.loc 3 982 0
	bl	FtlCacheWriteBack
.LVL1473:
.L1627:
	.loc 3 1005 0
	ldr	r3, [r5, #8]
	ldr	r5, .L1690+12
.LVL1474:
	cmp	r3, #0
	beq	.L1630
	.loc 3 1005 0 is_stmt 0 discriminator 1
	ldrh	r3, [r5, #4]
	.loc 3 1006 0 is_stmt 1 discriminator 1
	add	r0, r5, #48
	cmp	r3, #0
	moveq	r5, r0
.L1630:
	.loc 3 1009 0
	ldr	r4, .L1690
	.loc 3 1065 0
	mov	r6, r8
.LVL1475:
.L1631:
	.loc 3 1008 0
	ldr	r3, [fp, #-76]
.LVL1476:
	cmp	r3, #0
	beq	.L1665
	.loc 3 1009 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
.LVL1477:
	ldrh	r3, [r4, #8]
.LVL1478:
	cmp	r2, r3
	bcc	.L1632
	.loc 3 1009 0 is_stmt 0 discriminator 1
	movw	r2, #1009
	ldr	r1, .L1690+16
	ldr	r0, .L1690+20
	bl	printk
.LVL1479:
.L1632:
	.loc 3 1010 0 is_stmt 1
	ldrh	r10, [r5, #4]
	cmp	r10, #0
	bne	.L1633
	.loc 3 1011 0
	bl	FtlCacheWriteBack
.LVL1480:
	.loc 3 1013 0
	ldr	r3, .L1690+12
	ldr	r7, .L1690+4
	cmp	r5, r3
	bne	.L1634
	.loc 3 1014 0
	add	r0, r5, #48
	ldrh	r10, [r0, #4]
	cmp	r10, #0
	bne	.L1635
	.loc 3 1015 0
	bl	allocate_new_data_superblock
.LVL1481:
	.loc 3 1016 0
	str	r10, [r7, #8]
.L1635:
	.loc 3 1018 0
	ldr	r0, .L1690+12
	bl	allocate_new_data_superblock
.LVL1482:
	.loc 3 1020 0
	ldr	r5, .L1690+12
.LVL1483:
	.loc 3 1019 0
	ldr	r3, [r7, #8]
	.loc 3 1020 0
	add	r0, r5, #48
	cmp	r3, #0
	movne	r5, r0
.LVL1484:
.L1636:
	.loc 3 1030 0
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1633
	.loc 3 1031 0
	mov	r0, r5
	bl	allocate_new_data_superblock
.LVL1485:
.L1633:
	.loc 3 1040 0
	ldrh	r3, [r5, #4]
	ldr	r2, [fp, #-76]
.LVL1486:
	cmp	r3, r2
	movcs	r3, r2
	.loc 3 1038 0
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
.LVL1487:
	lsl	r2, r2, #2
	cmp	r3, r2
	movcs	r3, r2
	.loc 3 1042 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	str	r3, [fp, #-108]
	ldrh	r3, [r4, #8]
	cmp	r2, r3
	bcc	.L1637
	.loc 3 1042 0 is_stmt 0 discriminator 1
	movw	r2, #1042
	ldr	r1, .L1690+16
	ldr	r0, .L1690+20
	bl	printk
.LVL1488:
.L1637:
	.loc 3 1065 0 is_stmt 1 discriminator 1
	mov	r3, #0
.LVL1489:
.L1689:
	.loc 3 1043 0 discriminator 2
	str	r3, [fp, #-72]
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1659
.L1639:
	.loc 3 1124 0
	ldr	r3, [r4, #3948]
.LVL1490:
	cmp	r3, #0
	beq	.L1660
	.loc 3 1126 0
	ldr	r3, [fp, #-76]
.LVL1491:
	sub	r3, r3, #1
	str	r3, [fp, #-76]
	.loc 3 1127 0
	ldr	r3, [fp, #-72]
.LVL1492:
	subs	r3, r3, #1
	str	r3, [fp, #-72]
	bne	.L1660
.LVL1493:
.L1665:
	.loc 3 1134 0
	ldr	r3, [fp, #-92]
.LVL1494:
	mov	r0, #0
	sub	r1, r3, r8
	bl	rk_ftl_garbage_collect
.LVL1495:
	.loc 3 1136 0
	ldr	r2, .L1690
	movw	r3, #2276
	ldrh	r3, [r2, r3]
	cmp	r3, #15
	bhi	.L1662
	.loc 3 1138 0
	ldr	r4, .L1690+24
	ldr	r5, .L1690+28
.LVL1496:
.L1685:
	ldrh	r3, [r4]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1666
	.loc 3 1138 0 is_stmt 0 discriminator 1
	ldrh	r2, [r5]
	cmp	r2, r3
	bne	.L1666
	.loc 3 1139 0 is_stmt 1
	mov	r0, #0
	bl	List_get_gc_head_node
.LVL1497:
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.LVL1498:
.L1666:
	.loc 3 1140 0
	ldr	r3, .L1690+32
	.loc 3 1142 0
	mov	r1, #1
	.loc 3 1140 0
	mov	r2, #128
	.loc 3 1142 0
	mov	r0, r1
	.loc 3 1140 0
	strh	r2, [r3]	@ movhi
	.loc 3 1141 0
	strh	r2, [r3, #-2]	@ movhi
	.loc 3 1142 0
	bl	rk_ftl_garbage_collect
.LVL1499:
	.loc 3 1143 0
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1500:
	.loc 3 1144 0
	ldr	r3, .L1690+36
	ldrh	r3, [r3]
	cmp	r3, #8
	bls	.L1685
	b	.L1662
.LVL1501:
.L1628:
	.loc 3 984 0
	ldr	r3, [r10, #2452]
	.loc 3 985 0
	mov	r1, r4
	mov	r0, r9
	.loc 3 984 0
	add	r3, r3, #1
	str	r3, [r10, #2452]
	.loc 3 985 0
	bl	__aeabi_uidivmod
.LVL1502:
	ldr	r2, [fp, #-80]
.LVL1503:
	.loc 3 986 0
	sub	r4, r4, r1
	.loc 3 989 0
	ldr	r0, [r7, #8]
	.loc 3 985 0
	mov	r3, r1
	.loc 3 989 0
	ldr	r1, [fp, #-84]
	cmp	r2, r4
	movcc	r4, r2
	lsl	r10, r4, #9
	add	r0, r0, r3, lsl #9
	mov	r2, r10
.LVL1504:
	bl	ftl_memcpy
.LVL1505:
	.loc 3 991 0
	cmp	r6, #0
	bne	.L1629
.LVL1506:
.L1662:
	.loc 3 992 0
	mov	r0, #0
	b	.L1624
.LVL1507:
.L1629:
	.loc 3 993 0
	ldr	r3, [fp, #-80]
.LVL1508:
	.loc 3 994 0
	add	r9, r9, r4
	.loc 3 996 0
	add	r8, r8, #1
	.loc 3 993 0
	sub	r3, r3, r4
	str	r3, [fp, #-80]
	.loc 3 995 0
	ldr	r3, [fp, #-84]
.LVL1509:
	add	r3, r3, r10
	str	r3, [fp, #-84]
	.loc 3 997 0
	bl	FtlCacheWriteBack
.LVL1510:
	str	r6, [fp, #-76]
	b	.L1627
.LVL1511:
.L1634:
	.loc 3 1025 0
	ldrh	r2, [r3, #4]
	.loc 3 1024 0
	str	r10, [r7, #8]
	.loc 3 1025 0
	cmp	r2, #0
	movne	r5, r3
	bne	.L1633
	.loc 3 1026 0
	mov	r0, r5
	bl	allocate_new_data_superblock
.LVL1512:
	b	.L1636
.LVL1513:
.L1659:
	.loc 3 1044 0
	ldrh	r3, [r5, #4]
.LVL1514:
	cmp	r3, #0
	beq	.L1639
	.loc 3 1048 0
	mov	r2, #0
	sub	r1, fp, #68
	mov	r0, r6
	mov	r7, #20
	bl	log2phys
.LVL1515:
	.loc 3 1049 0
	mov	r0, r5
	bl	get_new_active_ppa
.LVL1516:
	.loc 3 1052 0
	ldrh	r2, [r4, #88]
	ldr	r1, [fp, #-72]
.LVL1517:
	ldr	r3, [fp, #-72]
.LVL1518:
	mul	r1, r2, r1
	mul	r7, r7, r3
	.loc 3 1051 0
	ldr	r3, [r4, #3800]
.LVL1519:
	.loc 3 1052 0
	bic	r1, r1, #3
	str	r1, [fp, #-100]
	.loc 3 1051 0
	add	r3, r3, r7
	.loc 3 1052 0
	ldr	r1, [r4, #3772]
	.loc 3 1051 0
	str	r0, [r3, #4]
	.loc 3 1052 0
	ldr	r0, [fp, #-100]
	str	r1, [fp, #-104]
	.loc 3 1053 0
	str	r6, [r3, #16]
	.loc 3 1052 0
	add	r10, r1, r0
	.loc 3 1056 0
	mov	r1, #0
	.loc 3 1052 0
	str	r10, [r3, #12]
	.loc 3 1056 0
	mov	r0, r10
	bl	ftl_memset
.LVL1520:
	.loc 3 1058 0
	ldr	r3, [fp, #-92]
.LVL1521:
	cmp	r6, r3
	cmpne	r6, r8
	ldrh	r3, [r4, #34]
.LVL1522:
	bne	.L1640
	.loc 3 1059 0
	cmp	r6, r8
	bne	.L1641
	.loc 3 1060 0
	mov	r1, r3
	mov	r0, r9
	str	r3, [fp, #-88]
	bl	__aeabi_uidivmod
.LVL1523:
	.loc 3 1061 0
	ldr	r3, [fp, #-88]
	ldr	r2, [fp, #-80]
.LVL1524:
	.loc 3 1060 0
	str	r1, [fp, #-96]
	.loc 3 1061 0
	sub	r3, r3, r1
	cmp	r3, r2
	movcs	r3, r2
	str	r3, [fp, #-88]
.L1642:
	.loc 3 1069 0
	ldrh	r3, [r4, #34]
	ldr	r2, [fp, #-88]
.LVL1525:
	cmp	r2, r3
	.loc 3 1071 0
	ldr	r3, [r4, #3800]
	.loc 3 1069 0
	bne	.L1643
	.loc 3 1070 0
	cmp	r6, r8
	.loc 3 1071 0
	add	r7, r3, r7
	ldreq	r3, [fp, #-84]
.LVL1526:
	.loc 3 1070 0
	beq	.L1687
	.loc 3 1073 0
	ldr	r3, [fp, #-88]
.LVL1527:
	mul	r2, r3, r6
	ldr	r3, [fp, #-84]
.LVL1528:
	sub	r2, r2, r9
	add	r2, r3, r2, lsl #9
	str	r2, [r7, #8]
.LVL1529:
.L1645:
	.loc 3 1116 0
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #8]
.LVL1530:
	cmp	r2, r3
	bcc	.L1656
	.loc 3 1116 0 is_stmt 0 discriminator 1
	movw	r2, #1116
	ldr	r1, .L1690+16
	ldr	r0, .L1690+20
	bl	printk
.LVL1531:
.L1656:
	.loc 3 1117 0 is_stmt 1
	ldr	r3, .L1690+40
	ldr	r2, [fp, #-104]
	ldr	r1, [fp, #-100]
	strh	r3, [r2, r1]	@ movhi
	.loc 3 1118 0
	ldr	r3, [r4, #2476]
	.loc 3 1119 0
	str	r6, [r10, #8]
	.loc 3 1122 0
	add	r6, r6, #1
	.loc 3 1118 0
	str	r3, [r10, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r4, #2476]
	.loc 3 1120 0
	ldr	r3, [fp, #-68]
.LVL1532:
	str	r3, [r10, #12]
	.loc 3 1121 0
	ldrh	r3, [r5]
.LVL1533:
	strh	r3, [r10, #2]	@ movhi
	.loc 3 1043 0
	ldr	r3, [fp, #-72]
.LVL1534:
	add	r3, r3, #1
	b	.L1689
.LVL1535:
.L1641:
	.loc 3 1066 0
	ldr	r2, [fp, #-80]
.LVL1536:
	smulbb	r3, r3, r6
	add	r2, r9, r2
.LVL1537:
	sub	r2, r2, r3
	uxth	r3, r2
	str	r3, [fp, #-88]
	.loc 3 1065 0
	mov	r3, #0
	str	r3, [fp, #-96]
	b	.L1642
.L1643:
	.loc 3 1076 0
	cmp	r6, r8
	.loc 3 1077 0
	add	r3, r3, r7
	ldreq	r2, [r4, #3776]
	.loc 3 1079 0
	ldrne	r2, [r4, #3768]
	str	r2, [r3, #8]
	.loc 3 1081 0
	ldr	r3, [fp, #-68]
	cmn	r3, #1
	beq	.L1648
.LBB516:
	.loc 3 1083 0
	str	r3, [fp, #-60]
	.loc 3 1087 0
	mov	r1, #1
	.loc 3 1085 0
	ldr	r3, [r4, #3800]
	.loc 3 1087 0
	sub	r0, fp, #64
	.loc 3 1084 0
	str	r6, [fp, #-48]
	.loc 3 1085 0
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	.loc 3 1086 0
	ldr	r3, [r3, #12]
	.loc 3 1085 0
	str	r2, [fp, #-56]
	.loc 3 1087 0
	mov	r2, #0
	.loc 3 1086 0
	str	r3, [fp, #-52]
	.loc 3 1087 0
	bl	FlashReadPages
.LVL1538:
	.loc 3 1088 0
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	.loc 3 1089 0
	ldreq	r3, [r4, #2688]
	addeq	r3, r3, #1
	streq	r3, [r4, #2688]
	.loc 3 1088 0
	beq	.L1652
	.loc 3 1091 0
	ldr	r3, [r10, #8]
	cmp	r6, r3
	beq	.L1651
	.loc 3 1092 0
	ldr	r3, [r4, #2688]
	.loc 3 1093 0
	mov	r2, r6
	ldr	r0, .L1690+44
	.loc 3 1092 0
	add	r3, r3, #1
	str	r3, [r4, #2688]
	.loc 3 1093 0
	ldr	r1, [r10, #8]
	bl	printk
.LVL1539:
.L1651:
	.loc 3 1095 0
	ldr	r3, [r10, #8]
	cmp	r6, r3
	beq	.L1652
	.loc 3 1095 0 is_stmt 0 discriminator 1
	movw	r2, #1095
	ldr	r1, .L1690+16
	ldr	r0, .L1690+20
	bl	printk
.LVL1540:
.L1652:
	ldr	r3, [fp, #-88]
.LBE516:
	.loc 3 1101 0 is_stmt 1
	cmp	r6, r8
	lsl	r2, r3, #9
	bne	.L1653
	.loc 3 1102 0
	ldr	r3, [r4, #3800]
	ldr	r1, [fp, #-84]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	ldr	r3, [fp, #-96]
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
.LVL1541:
	.loc 3 1106 0
	ldr	r3, [fp, #-92]
.LVL1542:
	cmp	r6, r3
	bne	.L1645
.L1654:
	.loc 3 1107 0
	ldrh	r3, [r5, #4]
.LVL1543:
	cmp	r3, #0
	.loc 3 1108 0
	ldrne	r3, [r4, #3800]
	addne	r7, r3, r7
	.loc 3 1109 0
	ldrne	r3, .L1690+8
	.loc 3 1108 0
	strne	r7, [r4, #3948]
	.loc 3 1109 0
	strne	r5, [r3, #-4076]
	b	.L1645
.L1648:
	.loc 3 1098 0
	ldr	r3, [r4, #3800]
	mov	r1, #0
	ldrh	r2, [r4, #86]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
.LVL1544:
	b	.L1652
.L1653:
	.loc 3 1104 0
	ldrh	r1, [r4, #34]
	ldr	r0, [fp, #-84]
.LVL1545:
	ldr	r3, [r4, #3800]
	mul	r1, r6, r1
	add	r3, r3, r7
	sub	r1, r1, r9
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
.LVL1546:
	bl	ftl_memcpy
.LVL1547:
	b	.L1654
.L1640:
	.loc 3 1114 0
	ldr	r2, [r4, #3800]
	mul	r3, r6, r3
	add	r7, r2, r7
	ldr	r2, [fp, #-84]
.LVL1548:
	sub	r3, r3, r9
	add	r3, r2, r3, lsl #9
.L1687:
	str	r3, [r7, #8]
	b	.L1645
.LVL1549:
.L1660:
	.loc 3 1130 0
	mov	r3, r5
.LVL1550:
	mov	r2, #0
	ldr	r1, [fp, #-72]
	ldr	r0, [r4, #3800]
	bl	FtlProgPages
.LVL1551:
	.loc 3 1131 0
	ldr	r3, [fp, #-76]
.LVL1552:
	ldr	r2, [fp, #-72]
.LVL1553:
	cmp	r3, r2
	bcs	.L1663
	.loc 3 1131 0 is_stmt 0 discriminator 1
	movw	r2, #1131
.LVL1554:
	ldr	r1, .L1690+16
	ldr	r0, .L1690+20
	bl	printk
.LVL1555:
.L1663:
	.loc 3 1132 0 is_stmt 1
	ldr	r3, [fp, #-76]
	ldr	r2, [fp, #-72]
	sub	r3, r3, r2
	str	r3, [fp, #-76]
	b	.L1631
.L1691:
	.align	2
.L1690:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2280
	.word	.LANCHOR1+574
	.word	.LC1
	.word	.LANCHOR0+2568
	.word	.LANCHOR0+3182
	.word	.LANCHOR0+3130
	.word	.LANCHOR0+2276
	.word	-3947
	.word	.LC101
.LFE157:
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
	.fnstart
.LFB160:
	.loc 3 1207 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1556:
	.movsp ip
	mov	ip, sp
.LCFI440:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI441:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI442:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 1207 0
	.loc 3 1208 0
	mov	r3, r2
	mov	r2, r1
.LVL1557:
	mov	r1, r0
.LVL1558:
	mov	r0, #0
.LVL1559:
	bl	FtlWrite
.LVL1560:
	.loc 3 1209 0
	ldmfd	sp, {fp, sp, pc}
.LFE160:
	.fnend
	.size	sftl_write, .-sftl_write
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
.LFB214:
	.loc 2 1360 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI443:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI444:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI445:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 1360 0
	.loc 2 1368 0
	mov	r1, #0
	.loc 2 1365 0
	ldr	r4, .L1714
	ldr	r6, .L1714+4
	ldr	r3, [r4, #2244]
	.loc 2 1368 0
	ldrh	r2, [r4, #16]
	ldr	r0, [r4, #2260]
	.loc 2 1365 0
	str	r3, [r4, #3232]
	.loc 2 1366 0
	ldr	r3, [r4, #3244]
	.loc 2 1368 0
	lsl	r2, r2, #1
	.loc 2 1366 0
	str	r3, [r4, #3236]
	.loc 2 1368 0
	bl	ftl_memset
.LVL1561:
	.loc 2 1369 0
	movw	r3, #2556
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1694
.L1702:
	.loc 2 1370 0
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1694:
	.loc 2 1378 0
	ldr	r7, .L1714+8
	.loc 2 1372 0
	mov	r1, #1
	.loc 2 1381 0
	ldr	r8, .L1714+12
	.loc 2 1372 0
	bl	FtlGetLastWrittenPage
.LVL1562:
	sxth	r5, r0
.LVL1563:
	.loc 2 1373 0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1696:
	.loc 2 1375 0
	cmp	r5, #0
	bge	.L1699
	.loc 2 1388 0
	movw	r2, #1388
	ldr	r1, .L1714+16
	ldr	r0, .L1714+20
	bl	printk
.LVL1564:
.L1698:
	.loc 2 1390 0
	ldrh	r3, [r4, #16]
	ldrh	r2, [r4, #86]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1701
	.loc 2 1390 0 is_stmt 0 discriminator 1
	movw	r2, #1390
	ldr	r1, .L1714+16
	ldr	r0, .L1714+20
	bl	printk
.LVL1565:
.L1701:
	.loc 2 1392 0 is_stmt 1
	ldr	r5, .L1714+24
.LVL1566:
	mov	r2, #48
	ldr	r1, [r4, #3232]
	mov	r0, r5
	bl	ftl_memcpy
.LVL1567:
	.loc 2 1393 0
	ldrh	r2, [r4, #16]
	ldr	r1, [r4, #3232]
	ldr	r0, [r4, #2260]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
.LVL1568:
	.loc 2 1394 0
	ldrh	r1, [r4, #16]
	ldr	r3, [r4, #3232]
	ldr	r0, [r4]
	lsr	r2, r1, #3
	add	r1, r1, #24
	lsl	r1, r1, #1
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
.LVL1569:
	.loc 2 1396 0
	ldr	r2, [r4, #2504]
	ldr	r3, .L1714+12
	cmp	r2, r3
	bne	.L1702
	.loc 2 1400 0
	ldrb	r2, [r4, #2514]	@ zero_extendqisi2
	ldrh	r3, [r4, #30]
	.loc 2 1399 0
	ldrh	r7, [r5, #8]
	.loc 2 1400 0
	cmp	r2, r3
	.loc 2 1399 0
	strh	r7, [r6, #6]	@ movhi
	.loc 2 1400 0
	bne	.L1702
	.loc 2 1403 0
	ldr	r3, .L1714+28
	.loc 2 1405 0
	ldrh	r2, [r4, #34]
	.loc 2 1406 0
	ldr	r6, [r4, #20]
	.loc 2 1403 0
	str	r7, [r3, #-4056]
	.loc 2 1404 0
	ldrh	r3, [r4, #78]
	.loc 2 1406 0
	ldrh	r0, [r4, #134]
	ldrh	r1, [r4, #8]
	.loc 2 1404 0
	mul	r3, r7, r3
	.loc 2 1406 0
	sub	r0, r6, r0
	sub	r0, r0, r7
	.loc 2 1404 0
	str	r3, [r4, #2436]
	.loc 2 1405 0
	mul	r3, r3, r2
	str	r3, [r4, #116]
	.loc 2 1406 0
	bl	__aeabi_uidiv
.LVL1570:
	.loc 2 1412 0
	cmp	r7, r6
	.loc 2 1406 0
	movw	r3, #2552
	strh	r0, [r4, r3]	@ movhi
	.loc 2 1412 0
	bls	.L1703
	.loc 2 1412 0 is_stmt 0 discriminator 1
	movw	r2, #1412
	ldr	r1, .L1714+16
	ldr	r0, .L1714+20
	bl	printk
.LVL1571:
.L1703:
	.loc 2 1414 0 is_stmt 1
	ldrh	r0, [r5, #14]
	movw	r3, #2280
	ldr	r1, .L1714+32
	.loc 2 1419 0
	mvn	ip, #0
	.loc 2 1414 0
	strh	r0, [r4, r3]	@ movhi
	.loc 2 1415 0
	ldrh	r3, [r5, #16]
	lsr	r2, r3, #6
	.loc 2 1416 0
	and	r3, r3, #63
	strb	r3, [r4, #2286]
	.loc 2 1417 0
	ldrb	r3, [r4, #2515]	@ zero_extendqisi2
	.loc 2 1415 0
	strh	r2, [r1, #2]	@ movhi
	.loc 2 1419 0
	add	r2, r1, #288
	.loc 2 1417 0
	strb	r3, [r4, #2288]
	.loc 2 1419 0
	movw	r3, #2568
	strh	ip, [r4, r3]	@ movhi
	.loc 2 1420 0
	mov	r3, #0
	.loc 2 1424 0
	ldrh	ip, [r5, #18]
	.loc 2 1420 0
	strh	r3, [r2, #2]	@ movhi
	.loc 2 1424 0
	movw	r2, #2328
	.loc 2 1421 0
	strb	r3, [r4, #2574]
	.loc 2 1424 0
	strh	ip, [r4, r2]	@ movhi
	.loc 2 1425 0
	ldrh	r2, [r5, #20]
	.loc 2 1422 0
	strb	r3, [r4, #2576]
	.loc 2 1434 0
	str	r3, [r4, #2460]
	.loc 2 1435 0
	str	r3, [r4, #2448]
	.loc 2 1425 0
	lsr	ip, r2, #6
	.loc 2 1426 0
	and	r2, r2, #63
	strb	r2, [r4, #2334]
	.loc 2 1427 0
	ldrb	r2, [r4, #2516]	@ zero_extendqisi2
	.loc 2 1425 0
	strh	ip, [r1, #50]	@ movhi
	.loc 2 1429 0
	ldrh	ip, [r5, #22]
	.loc 2 1427 0
	strb	r2, [r4, #2336]
	.loc 2 1429 0
	movw	r2, #2376
	.loc 2 1436 0
	str	r3, [r4, #2440]
	.loc 2 1429 0
	strh	ip, [r4, r2]	@ movhi
	.loc 2 1430 0
	ldrh	r2, [r5, #24]
	.loc 2 1437 0
	str	r3, [r4, #2456]
	.loc 2 1440 0
	str	r3, [r4, #2484]
	.loc 2 1430 0
	lsr	ip, r2, #6
	.loc 2 1431 0
	and	r2, r2, #63
	strb	r2, [r4, #2382]
	.loc 2 1432 0
	ldrb	r2, [r4, #2517]	@ zero_extendqisi2
	.loc 2 1430 0
	strh	ip, [r1, #98]	@ movhi
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
	strhi	r3, [r4, #2472]
	.loc 2 1447 0
	ldr	r3, [r4, #2540]
	cmp	r3, r2
	.loc 2 1448 0
	strhi	r3, [r4, #2476]
	.loc 2 1450 0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1706
	.loc 2 1451 0
	ldr	r0, .L1714+32
	bl	make_superblock
.LVL1572:
.L1706:
	.loc 2 1453 0
	movw	r3, #2328
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1707
	.loc 2 1454 0
	ldr	r0, .L1714+36
	bl	make_superblock
.LVL1573:
.L1707:
	.loc 2 1456 0
	movw	r3, #2376
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1708
	.loc 2 1457 0
	ldr	r0, .L1714+40
	bl	make_superblock
.LVL1574:
.L1708:
	.loc 2 1459 0
	movw	r3, #2568
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1709
	.loc 2 1460 0
	ldr	r0, .L1714+44
	bl	make_superblock
.LVL1575:
.L1709:
	.loc 2 1462 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.LVL1576:
.L1699:
	.loc 2 1376 0
	ldrh	r3, [r6]
	.loc 2 1378 0
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	.loc 2 1376 0
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3228]
	.loc 2 1377 0
	ldr	r3, [r4, #2244]
	str	r3, [r4, #3232]
	.loc 2 1378 0
	bl	FlashReadPages
.LVL1577:
	.loc 2 1381 0
	ldr	r3, [r4, #3224]
	cmn	r3, #1
	beq	.L1697
	.loc 2 1382 0 discriminator 1
	ldr	r3, [r4, #2244]
	.loc 2 1381 0 discriminator 1
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1697
	.loc 2 1383 0
	ldr	r3, [r4, #3244]
	ldrh	r2, [r3]
	.loc 2 1382 0
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1698
.L1697:
	sub	r5, r5, #1
.LVL1578:
	sxth	r5, r5
.LVL1579:
	b	.L1696
.L1715:
	.align	2
.L1714:
	.word	.LANCHOR0
	.word	.LANCHOR0+2556
	.word	.LANCHOR0+3224
	.word	1179929683
	.word	.LANCHOR1+583
	.word	.LC1
	.word	.LANCHOR0+2504
	.word	.LANCHOR3
	.word	.LANCHOR0+2280
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2376
	.word	.LANCHOR0+2568
.LFE214:
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlSysBlkInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
.LFB232:
	.loc 2 2306 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI446:
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, r6, r7, r8, r9, fp, ip, lr}
.LCFI447:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI448:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 2 2306 0
	.loc 2 2310 0
	mov	r3, #0
	ldr	r4, .L1732
	add	r6, r4, #3968
	.loc 2 2312 0
	ldrh	r0, [r4, #12]
	.loc 2 2310 0
	strh	r3, [r6]	@ movhi
	.loc 2 2312 0
	bl	FtlFreeSysBlkQueueInit
.LVL1580:
	.loc 2 2313 0
	bl	FtlScanSysBlk
.LVL1581:
	.loc 2 2314 0
	movw	r3, #2556
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1717
.L1719:
	.loc 2 2315 0
	mvn	r8, #0
.L1716:
	.loc 2 2398 0
	mov	r0, r8
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1717:
	.loc 2 2317 0
	bl	FtlLoadSysInfo
.LVL1582:
	subs	r8, r0, #0
	bne	.L1719
	.loc 2 2320 0
	bl	FtlLoadMapInfo
.LVL1583:
	.loc 2 2321 0
	bl	FtlLoadVonderInfo
.LVL1584:
	.loc 2 2322 0
	bl	Ftl_load_ext_data
.LVL1585:
	.loc 2 2324 0
	bl	FtlLoadEctTbl
.LVL1586:
	.loc 2 2325 0
	bl	FtlFreeSysBLkSort
.LVL1587:
	.loc 2 2327 0
	bl	SupperBlkListInit
.LVL1588:
	.loc 2 2328 0
	bl	FtlPowerLostRecovery
.LVL1589:
	.loc 2 2330 0
	mov	r0, #1
	bl	FtlUpdateVaildLpn
.LVL1590:
.LBB520:
.LBB521:
	.loc 2 2332 0
	ldrh	r1, [r4, #114]
	mov	r3, r8
.LVL1591:
	.loc 2 2333 0
	ldr	r2, [r4, #2424]
	mov	r0, #12
.L1720:
	.loc 2 2332 0
	cmp	r3, r1
	bge	.L1725
	.loc 2 2333 0
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1721
.L1725:
	.loc 2 2336 0
	ldr	r5, .L1732+4
	.loc 2 2338 0
	cmp	r3, r1
	.loc 2 2336 0
	ldrh	r2, [r5, #28]
	sub	r7, r5, #224
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	.loc 2 2338 0
	bge	.L1731
.L1722:
	.loc 2 2340 0
	ldr	r6, .L1732+8
	.loc 2 2339 0
	ldr	r0, .L1732+12
	bl	FtlSuperblockPowerLostFix
.LVL1592:
	.loc 2 2340 0
	mov	r0, r6
	bl	FtlSuperblockPowerLostFix
.LVL1593:
.LBB522:
	.loc 2 2346 0
	movw	r3, #2280
	ldr	r1, [r4, #2260]
	ldrh	r3, [r4, r3]
	ldrh	r0, [r7, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	.loc 2 2352 0
	movw	r2, #2328
	ldrh	r2, [r4, r2]
	.loc 2 2347 0
	ldrh	r3, [r4, #78]
	.loc 2 2352 0
	ldr	r0, [r4, #2260]
	ldrh	ip, [r6, #4]
	lsl	r2, r2, #1
	.loc 2 2347 0
	strh	r3, [r7, #2]	@ movhi
	.loc 2 2348 0
	mov	r3, #0
	strb	r3, [r4, #2286]
	.loc 2 2349 0
	strh	r3, [r7, #4]	@ movhi
	.loc 2 2352 0
	ldrh	r1, [r0, r2]
	sub	r1, r1, ip
	strh	r1, [r0, r2]	@ movhi
.LBE522:
	.loc 2 2359 0
	add	r0, r6, #1536
.LBB523:
	.loc 2 2353 0
	ldrh	r2, [r4, #78]
	.loc 2 2354 0
	strb	r3, [r4, #2334]
	.loc 2 2355 0
	strh	r3, [r6, #4]	@ movhi
	.loc 2 2353 0
	strh	r2, [r6, #2]	@ movhi
.LBE523:
	.loc 2 2359 0
	bl	FtlMapBlkWriteDump_data
.LVL1594:
	.loc 2 2360 0
	ldr	r0, .L1732+16
	bl	FtlMapBlkWriteDump_data
.LVL1595:
	.loc 2 2372 0
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	.loc 2 2373 0
	bl	l2p_flush
.LVL1596:
	.loc 2 2374 0
	bl	FtlVpcTblFlush
.LVL1597:
	.loc 2 2375 0
	bl	FtlVpcTblFlush
.LVL1598:
	b	.L1726
.LVL1599:
.L1721:
	.loc 2 2332 0
	add	r3, r3, #1
	b	.L1720
.L1731:
	.loc 2 2338 0
	ldrh	r3, [r6]
.LVL1600:
	cmp	r3, #0
	bne	.L1722
.L1726:
	.loc 2 2379 0
	movw	r3, #2280
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1727
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L1727
	.loc 2 2381 0
	ldr	r6, .L1732+8
	.loc 2 2380 0
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L1727
	.loc 2 2382 0
	bl	FtlGcRefreshBlock
.LVL1601:
	.loc 2 2383 0
	movw	r3, #2328
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
.LVL1602:
	.loc 2 2384 0
	sub	r0, r6, #48
	bl	allocate_new_data_superblock
.LVL1603:
	.loc 2 2385 0
	mov	r0, r6
	bl	allocate_new_data_superblock
.LVL1604:
.L1727:
	.loc 2 2390 0
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L1716
	.loc 2 2391 0
	bl	FtlVpcCheckAndModify
.LVL1605:
	b	.L1716
.L1733:
	.align	2
.L1732:
	.word	.LANCHOR0
	.word	.LANCHOR0+2504
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2280
	.word	.LANCHOR0+3908
.LBE521:
.LBE520:
.LFE232:
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
	.fnstart
.LFB150:
	.loc 3 623 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI449:
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	.pad #4
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, ip, lr}
.LCFI450:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI451:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 623 0
	.loc 3 629 0
	mov	r3, #0
	ldr	r4, .L1758
	.loc 3 631 0
	ldrh	r0, [r4, #12]
	.loc 3 629 0
	str	r3, [r4, #2472]
	.loc 3 630 0
	str	r3, [r4, #2476]
	.loc 3 631 0
	bl	FtlFreeSysBlkQueueInit
.LVL1606:
	.loc 3 632 0
	bl	FtlLoadBbt
.LVL1607:
	cmp	r0, #0
	beq	.L1735
	.loc 3 633 0
	bl	FtlMakeBbt
.LVL1608:
.L1735:
	.loc 3 637 0 discriminator 1
	ldr	r0, .L1758+4
	.loc 3 623 0 discriminator 1
	mov	r2, #0
.L1736:
	.loc 3 635 0 discriminator 1
	ldrh	r1, [r4, #34]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1737
	.loc 3 642 0
	ldrh	r6, [r4, #16]
	.loc 3 641 0
	mov	r5, #0
.LVL1609:
.L1738:
	.loc 3 642 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r6
	bhi	.L1739
	.loc 3 645 0
	ldrh	r1, [r4, #8]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1740
.L1744:
	.loc 3 641 0
	mov	r5, #0
.LVL1610:
	mov	r6, r5
.LVL1611:
.L1741:
	.loc 3 656 0 discriminator 1
	ldrh	r3, [r4, #16]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bhi	.L1745
	.loc 3 658 0
	ldrh	r3, [r4, #18]
	.loc 3 662 0
	movw	r9, #2552
	.loc 3 661 0
	ldrh	r5, [r4, #8]
	ldr	r10, [r4, #20]
	.loc 3 658 0
	str	r3, [r4, #3764]
	.loc 3 661 0
	mov	r1, r5
	mov	r0, r10
	bl	__aeabi_uidiv
.LVL1612:
	.loc 3 662 0
	ubfx	r7, r0, #5, #16
	.loc 3 661 0
	mov	r8, r0
	.loc 3 662 0
	add	r3, r7, #36
	.loc 3 661 0
	str	r0, [r4, #2436]
	.loc 3 662 0
	strh	r3, [r4, r9]	@ movhi
	.loc 3 663 0
	mov	r3, #24
	mul	r3, r3, r5
	cmp	r6, r3
	ble	.L1746
	.loc 3 664 0
	mov	r1, r5
	sub	r0, r10, r6
	bl	__aeabi_uidiv
.LVL1613:
	str	r0, [r4, #2436]
	.loc 3 665 0
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r4, r9]	@ movhi
.L1746:
	.loc 3 668 0
	ldrh	r3, [r4, #70]
	cmp	r3, #0
	beq	.L1748
	.loc 3 669 0
	movw	r2, #2552
	ldrh	r1, [r4, r2]
	add	r1, r1, r3, lsr #1
	strh	r1, [r4, r2]	@ movhi
	.loc 3 670 0
	mul	r1, r5, r3
	cmp	r6, r1
	.loc 3 672 0
	addlt	r3, r3, #32
	.loc 3 671 0
	strlt	r8, [r4, #2436]
	.loc 3 672 0
	addlt	r3, r7, r3
	strhlt	r3, [r4, r2]	@ movhi
.L1748:
	.loc 3 677 0
	movw	r3, #2552
	ldr	r6, .L1758+8
.LVL1614:
	ldrh	r2, [r4, r3]
	.loc 3 689 0
	mvn	r7, #0
	.loc 3 677 0
	ldr	r3, [r4, #2436]
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
	str	r5, [r4, #2436]
	.loc 3 679 0
	mul	r5, r5, r3
	str	r5, [r4, #116]
	.loc 3 684 0
	bl	FtlBbmTblFlush
.LVL1615:
	.loc 3 687 0
	ldrh	r2, [r4, #18]
	mov	r1, #0
	ldr	r0, [r4, #2260]
	.loc 3 689 0
	ldr	r5, .L1758+12
	.loc 3 687 0
	lsl	r2, r2, #1
	bl	ftl_memset
.LVL1616:
	.loc 3 689 0
	movw	r2, #2568
	.loc 3 688 0
	mov	r3, #0
	.loc 3 689 0
	strh	r7, [r4, r2]	@ movhi
	.loc 3 696 0
	movw	r2, #2280
	strh	r3, [r4, r2]	@ movhi
	.loc 3 698 0
	mov	r1, #255
	ldrh	r2, [r4, #16]
	.loc 3 690 0
	strh	r3, [r5, #2]	@ movhi
	.loc 3 694 0
	sub	r5, r5, #288
	.loc 3 701 0
	mov	r8, r5
	.loc 3 688 0
	str	r3, [r4, #2432]
	.loc 3 691 0
	strb	r3, [r4, #2574]
	.loc 3 692 0
	strb	r3, [r4, #2576]
	.loc 3 698 0
	lsr	r2, r2, #3
	.loc 3 694 0
	strh	r3, [r5, #2]	@ movhi
	.loc 3 695 0
	strb	r3, [r4, #2286]
	.loc 3 697 0
	mov	r3, #1
	.loc 3 698 0
	ldr	r0, [r4]
	.loc 3 697 0
	strb	r3, [r4, #2288]
	.loc 3 698 0
	bl	ftl_memset
.LVL1617:
.L1750:
	.loc 3 701 0
	mov	r0, r8
	bl	make_superblock
.LVL1618:
	.loc 3 702 0
	ldrb	r3, [r4, #2287]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1751
	.loc 3 705 0
	ldr	r2, [r4, #2260]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	.loc 3 706 0
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	.loc 3 707 0
	b	.L1750
.L1737:
	.loc 3 636 0 discriminator 3
	ldr	ip, [r4, #3776]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	.loc 3 637 0 discriminator 3
	ldr	r1, [r4, #3768]
	str	r0, [r1, r3, lsl #2]
	b	.L1736
.LVL1619:
.L1739:
	.loc 3 643 0 discriminator 3
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
.LVL1620:
	.loc 3 642 0 discriminator 3
	add	r6, r6, #1
	.loc 3 643 0 discriminator 3
	add	r5, r5, r0
.LVL1621:
	uxth	r5, r5
.LVL1622:
	.loc 3 642 0 discriminator 3
	uxth	r6, r6
	b	.L1738
.L1740:
	.loc 3 647 0
	mov	r0, r5
	bl	__aeabi_uidiv
.LVL1623:
	ldr	r3, [r4, #108]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
.LVL1624:
	.loc 3 648 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1625:
	.loc 3 650 0
	ldrh	r5, [r4, #16]
.LVL1626:
.L1742:
	.loc 3 650 0 is_stmt 0 discriminator 1
	ldrh	r3, [r4, #18]
	cmp	r3, r5
	bls	.L1744
	.loc 3 651 0 is_stmt 1 discriminator 3
	mov	r0, r5
	mov	r1, #1
	.loc 3 650 0 discriminator 3
	add	r5, r5, #1
	.loc 3 651 0 discriminator 3
	bl	FtlLowFormatEraseBlock
.LVL1627:
	.loc 3 650 0 discriminator 3
	uxth	r5, r5
	b	.L1742
.LVL1628:
.L1745:
	.loc 3 657 0 discriminator 3
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
.LVL1629:
	add	r6, r6, r0
.LVL1630:
	uxth	r6, r6
.LVL1631:
	b	.L1741
.LVL1632:
.L1751:
	.loc 3 709 0
	ldr	r2, [r4, #2472]
	.loc 3 710 0
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	.loc 3 721 0
	mvn	r7, #0
	.loc 3 709 0
	str	r2, [r4, #2292]
	add	r2, r2, #1
	str	r2, [r4, #2472]
	.loc 3 710 0
	ldr	r2, [r4, #2260]
	strh	r1, [r2, r3]	@ movhi
	.loc 3 712 0
	mov	r3, #0
	ldr	r2, .L1758+16
	.loc 3 714 0
	movw	r1, #2328
	.loc 3 713 0
	strb	r3, [r4, #2334]
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
	strb	r3, [r4, #2336]
.L1752:
	.loc 3 717 0
	mov	r0, r8
	bl	make_superblock
.LVL1633:
	.loc 3 718 0
	ldrb	r3, [r4, #2335]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1753
	.loc 3 721 0
	ldr	r2, [r4, #2260]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	.loc 3 722 0
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	.loc 3 723 0
	b	.L1752
.L1753:
	.loc 3 725 0
	ldr	r2, [r4, #2472]
	.loc 3 726 0
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	.loc 3 727 0
	mvn	r5, #0
	.loc 3 725 0
	str	r2, [r4, #2340]
	add	r2, r2, #1
	str	r2, [r4, #2472]
	.loc 3 726 0
	ldr	r2, [r4, #2260]
	strh	r1, [r2, r3]	@ movhi
	.loc 3 727 0
	movw	r3, #2376
	strh	r5, [r4, r3]	@ movhi
	.loc 3 730 0
	bl	FtlFreeSysBlkQueueOut
.LVL1634:
	ldr	r3, .L1758+20
	movw	r2, #2556
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
	ldr	r3, [r4, #2472]
	str	r3, [r4, #2564]
	add	r3, r3, #1
	str	r3, [r4, #2472]
	.loc 3 735 0
	bl	FtlVpcTblFlush
.LVL1635:
	.loc 3 736 0
	bl	FtlSysBlkInit
.LVL1636:
	cmp	r0, #0
	.loc 3 739 0
	mov	r0, #0
	.loc 3 737 0
	ldreq	r3, .L1758+24
	moveq	r2, #1
	streq	r2, [r3]
	.loc 3 739 0
	ldmfd	sp, {r3, r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1759:
	.align	2
.L1758:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR3
	.word	.LANCHOR0+2568
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2556
	.word	.LANCHOR2
.LFE150:
	.fnend
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
	.fnstart
.LFB151:
	.loc 3 743 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	.movsp ip
	mov	ip, sp
.LCFI452:
	push	{r4, r5, fp, ip, lr, pc}
	.pad #4
	.save {r4, r5, fp, ip, lr}
.LCFI453:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI454:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 3 743 0
	.loc 3 746 0
	mvn	r3, #0
	ldr	r5, .L1766
	.loc 3 753 0
	ldr	r4, .L1766+4
	.loc 3 749 0
	ldr	r1, .L1766+8
	ldr	r0, .L1766+12
	.loc 3 746 0
	str	r3, [r5]
	.loc 3 749 0
	bl	printk
.LVL1637:
	.loc 3 750 0
	ldr	r0, .L1766+16
	bl	FtlConstantsInit
.LVL1638:
	.loc 3 751 0
	bl	FtlMemInit
.LVL1639:
	.loc 3 752 0
	bl	FtlVariablesInit
.LVL1640:
	.loc 3 753 0
	ldrh	r0, [r4, #12]
	bl	FtlFreeSysBlkQueueInit
.LVL1641:
	.loc 3 756 0
	bl	FtlLoadBbt
.LVL1642:
	cmp	r0, #0
	bne	.L1764
	.loc 3 767 0
	bl	FtlSysBlkInit
.LVL1643:
	cmp	r0, #0
	bne	.L1764
	.loc 3 779 0
	mov	r3, #1
	str	r3, [r5]
	.loc 3 783 0
	movw	r3, #2276
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L1764
	movw	r4, #8129
.L1763:
.LBB524:
	.loc 3 786 0 discriminator 3
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
.LVL1644:
	.loc 3 785 0 discriminator 3
	subs	r4, r4, #1
	bne	.L1763
.L1764:
.LBE524:
	.loc 3 794 0
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1767:
	.align	2
.L1766:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC70
	.word	.LC71
	.word	g_nand_phy_info
.LFE151:
	.fnend
	.size	sftl_init, .-sftl_init
	.align	2
	.global	ftl_memcmp
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcmp, %function
ftl_memcmp:
	.fnstart
.LFB269:
	.loc 1 211 0
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
.LVL1645:
	.movsp ip
	mov	ip, sp
.LCFI455:
	push	{fp, ip, lr, pc}
	.pad #4
	.save {fp, ip, lr}
.LCFI456:
	.setfp fp, ip, #-4
	sub	fp, ip, #4
.LCFI457:
	push	{lr}
	bl	__gnu_mcount_nc
	.loc 1 211 0
	.loc 1 212 0
	bl	memcmp
.LVL1646:
	.loc 1 213 0
	ldmfd	sp, {fp, sp, pc}
.LFE269:
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
	.type	__func__.6704, %object
	.size	__func__.6704, 17
__func__.6704:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6699, %object
	.size	__func__.6699, 17
__func__.6699:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6734, %object
	.size	__func__.6734, 17
__func__.6734:
	.ascii	"List_remove_node\000"
	.type	__func__.6766, %object
	.size	__func__.6766, 22
__func__.6766:
	.ascii	"List_update_data_list\000"
	.type	__func__.6807, %object
	.size	__func__.6807, 26
__func__.6807:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6871, %object
	.size	__func__.6871, 22
__func__.6871:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7194, %object
	.size	__func__.7194, 16
__func__.7194:
	.ascii	"make_superblock\000"
	.type	__func__.7368, %object
	.size	__func__.7368, 19
__func__.7368:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.7608, %object
	.size	__func__.7608, 15
__func__.7608:
	.ascii	"FlashReadPages\000"
	.type	__func__.7625, %object
	.size	__func__.7625, 15
__func__.7625:
	.ascii	"FlashProgPages\000"
	.type	__func__.6317, %object
	.size	__func__.6317, 11
__func__.6317:
	.ascii	"FtlMemInit\000"
	.type	__func__.6533, %object
	.size	__func__.6533, 14
__func__.6533:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.6775, %object
	.size	__func__.6775, 16
__func__.6775:
	.ascii	"load_l2p_region\000"
	.type	__func__.6832, %object
	.size	__func__.6832, 31
__func__.6832:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6846, %object
	.size	__func__.6846, 16
__func__.6846:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6818, %object
	.size	__func__.6818, 15
__func__.6818:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6888, %object
	.size	__func__.6888, 9
__func__.6888:
	.ascii	"log2phys\000"
	.type	__func__.7103, %object
	.size	__func__.7103, 16
__func__.7103:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7256, %object
	.size	__func__.7256, 14
__func__.7256:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.6983, %object
	.size	__func__.6983, 14
__func__.6983:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7215, %object
	.size	__func__.7215, 18
__func__.7215:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.7340, %object
	.size	__func__.7340, 25
__func__.7340:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7381, %object
	.size	__func__.7381, 16
__func__.7381:
	.ascii	"update_vpc_list\000"
	.type	__func__.7388, %object
	.size	__func__.7388, 20
__func__.7388:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7137, %object
	.size	__func__.7137, 22
__func__.7137:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.6576, %object
	.size	__func__.6576, 11
__func__.6576:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.6961, %object
	.size	__func__.6961, 15
__func__.6961:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7240, %object
	.size	__func__.7240, 21
__func__.7240:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7361, %object
	.size	__func__.7361, 29
__func__.7361:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6441, %object
	.size	__func__.6441, 13
__func__.6441:
	.ascii	"FtlProgPages\000"
	.type	__func__.7458, %object
	.size	__func__.7458, 19
__func__.7458:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7564, %object
	.size	__func__.7564, 23
__func__.7564:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6469, %object
	.size	__func__.6469, 9
__func__.6469:
	.ascii	"FtlWrite\000"
	.type	__func__.7040, %object
	.size	__func__.7040, 15
__func__.7040:
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
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.byte	0x4
	.4byte	.LCFI0-.LFB261
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
	.4byte	.LFB276
	.4byte	.LFE276-.LFB276
	.byte	0x4
	.4byte	.LCFI3-.LFB276
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
	.4byte	.LFB142
	.4byte	.LFE142-.LFB142
	.byte	0x4
	.4byte	.LCFI6-.LFB142
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
	.4byte	.LFB143
	.4byte	.LFE143-.LFB143
	.byte	0x4
	.4byte	.LCFI9-.LFB143
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
	.4byte	.LFB144
	.4byte	.LFE144-.LFB144
	.byte	0x4
	.4byte	.LCFI12-.LFB144
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
	.4byte	.LFB145
	.4byte	.LFE145-.LFB145
	.byte	0x4
	.4byte	.LCFI15-.LFB145
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI16-.LCFI15
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LFB148
	.4byte	.LFE148-.LFB148
	.byte	0x4
	.4byte	.LCFI18-.LFB148
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
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.byte	0x4
	.4byte	.LCFI21-.LFB161
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
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.byte	0x4
	.4byte	.LCFI24-.LFB162
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
	.byte	0x4
	.4byte	.LCFI27-.LCFI26
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x20
	.align	2
.LEFDE16:
.LSFDE18:
	.4byte	.LEFDE18-.LASFDE18
.LASFDE18:
	.4byte	.Lframe0
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.byte	0x4
	.4byte	.LCFI28-.LFB163
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI29-.LCFI28
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
	.4byte	.LCFI30-.LCFI29
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE18:
.LSFDE20:
	.4byte	.LEFDE20-.LASFDE20
.LASFDE20:
	.4byte	.Lframe0
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.byte	0x4
	.4byte	.LCFI31-.LFB164
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI32-.LCFI31
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI33-.LCFI32
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE20:
.LSFDE22:
	.4byte	.LEFDE22-.LASFDE22
.LASFDE22:
	.4byte	.Lframe0
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.byte	0x4
	.4byte	.LCFI34-.LFB167
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI35-.LCFI34
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI36-.LCFI35
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE22:
.LSFDE24:
	.4byte	.LEFDE24-.LASFDE24
.LASFDE24:
	.4byte	.Lframe0
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.byte	0x4
	.4byte	.LCFI37-.LFB168
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI38-.LCFI37
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
	.4byte	.LCFI39-.LCFI38
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE24:
.LSFDE26:
	.4byte	.LEFDE26-.LASFDE26
.LASFDE26:
	.4byte	.Lframe0
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.byte	0x4
	.4byte	.LCFI40-.LFB172
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI41-.LCFI40
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
	.4byte	.LCFI42-.LCFI41
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE26:
.LSFDE28:
	.4byte	.LEFDE28-.LASFDE28
.LASFDE28:
	.4byte	.Lframe0
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.byte	0x4
	.4byte	.LCFI43-.LFB173
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI44-.LCFI43
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
	.4byte	.LCFI45-.LCFI44
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE28:
.LSFDE30:
	.4byte	.LEFDE30-.LASFDE30
.LASFDE30:
	.4byte	.Lframe0
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.byte	0x4
	.4byte	.LCFI46-.LFB174
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI47-.LCFI46
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
	.4byte	.LCFI48-.LCFI47
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE30:
.LSFDE32:
	.4byte	.LEFDE32-.LASFDE32
.LASFDE32:
	.4byte	.Lframe0
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.byte	0x4
	.4byte	.LCFI49-.LFB175
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI50-.LCFI49
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI51-.LCFI50
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE32:
.LSFDE34:
	.4byte	.LEFDE34-.LASFDE34
.LASFDE34:
	.4byte	.Lframe0
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.byte	0x4
	.4byte	.LCFI52-.LFB177
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI53-.LCFI52
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
	.4byte	.LCFI54-.LCFI53
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE34:
.LSFDE36:
	.4byte	.LEFDE36-.LASFDE36
.LASFDE36:
	.4byte	.Lframe0
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.byte	0x4
	.4byte	.LCFI55-.LFB178
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI56-.LCFI55
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI57-.LCFI56
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE36:
.LSFDE38:
	.4byte	.LEFDE38-.LASFDE38
.LASFDE38:
	.4byte	.Lframe0
	.4byte	.LFB179
	.4byte	.LFE179-.LFB179
	.byte	0x4
	.4byte	.LCFI58-.LFB179
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI59-.LCFI58
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI60-.LCFI59
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE38:
.LSFDE40:
	.4byte	.LEFDE40-.LASFDE40
.LASFDE40:
	.4byte	.Lframe0
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.byte	0x4
	.4byte	.LCFI61-.LFB181
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI62-.LCFI61
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
	.4byte	.LCFI63-.LCFI62
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE40:
.LSFDE42:
	.4byte	.LEFDE42-.LASFDE42
.LASFDE42:
	.4byte	.Lframe0
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.byte	0x4
	.4byte	.LCFI64-.LFB182
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI65-.LCFI64
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI66-.LCFI65
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE42:
.LSFDE44:
	.4byte	.LEFDE44-.LASFDE44
.LASFDE44:
	.4byte	.Lframe0
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.byte	0x4
	.4byte	.LCFI67-.LFB183
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI68-.LCFI67
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI69-.LCFI68
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE44:
.LSFDE46:
	.4byte	.LEFDE46-.LASFDE46
.LASFDE46:
	.4byte	.Lframe0
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.byte	0x4
	.4byte	.LCFI70-.LFB186
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI71-.LCFI70
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
	.4byte	.LCFI72-.LCFI71
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI73-.LCFI72
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI74-.LCFI73
	.byte	0xb
	.align	2
.LEFDE46:
.LSFDE48:
	.4byte	.LEFDE48-.LASFDE48
.LASFDE48:
	.4byte	.Lframe0
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.byte	0x4
	.4byte	.LCFI75-.LFB185
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI76-.LCFI75
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI77-.LCFI76
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE48:
.LSFDE50:
	.4byte	.LEFDE50-.LASFDE50
.LASFDE50:
	.4byte	.Lframe0
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.byte	0x4
	.4byte	.LCFI78-.LFB187
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI79-.LCFI78
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LCFI80-.LCFI79
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE50:
.LSFDE52:
	.4byte	.LEFDE52-.LASFDE52
.LASFDE52:
	.4byte	.Lframe0
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.byte	0x4
	.4byte	.LCFI81-.LFB184
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI82-.LCFI81
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI83-.LCFI82
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE52:
.LSFDE54:
	.4byte	.LEFDE54-.LASFDE54
.LASFDE54:
	.4byte	.Lframe0
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.byte	0x4
	.4byte	.LCFI84-.LFB188
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI85-.LCFI84
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
	.4byte	.LCFI86-.LCFI85
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE54:
.LSFDE56:
	.4byte	.LEFDE56-.LASFDE56
.LASFDE56:
	.4byte	.Lframe0
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.byte	0x4
	.4byte	.LCFI87-.LFB189
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI88-.LCFI87
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
	.4byte	.LCFI89-.LCFI88
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE56:
.LSFDE58:
	.4byte	.LEFDE58-.LASFDE58
.LASFDE58:
	.4byte	.Lframe0
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.byte	0x4
	.4byte	.LCFI90-.LFB190
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI91-.LCFI90
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI92-.LCFI91
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE58:
.LSFDE60:
	.4byte	.LEFDE60-.LASFDE60
.LASFDE60:
	.4byte	.Lframe0
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.byte	0x4
	.4byte	.LCFI93-.LFB191
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI94-.LCFI93
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI95-.LCFI94
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE60:
.LSFDE62:
	.4byte	.LEFDE62-.LASFDE62
.LASFDE62:
	.4byte	.Lframe0
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.byte	0x4
	.4byte	.LCFI96-.LFB192
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI97-.LCFI96
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
	.4byte	.LCFI98-.LCFI97
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE62:
.LSFDE64:
	.4byte	.LEFDE64-.LASFDE64
.LASFDE64:
	.4byte	.Lframe0
	.4byte	.LFB195
	.4byte	.LFE195-.LFB195
	.byte	0x4
	.4byte	.LCFI99-.LFB195
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI100-.LCFI99
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
	.4byte	.LCFI101-.LCFI100
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE64:
.LSFDE66:
	.4byte	.LEFDE66-.LASFDE66
.LASFDE66:
	.4byte	.Lframe0
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.byte	0x4
	.4byte	.LCFI102-.LFB200
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI103-.LCFI102
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
	.4byte	.LCFI104-.LCFI103
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE66:
.LSFDE68:
	.4byte	.LEFDE68-.LASFDE68
.LASFDE68:
	.4byte	.Lframe0
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.byte	0x4
	.4byte	.LCFI105-.LFB206
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI106-.LCFI105
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI107-.LCFI106
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE68:
.LSFDE70:
	.4byte	.LEFDE70-.LASFDE70
.LASFDE70:
	.4byte	.Lframe0
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.byte	0x4
	.4byte	.LCFI108-.LFB212
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI109-.LCFI108
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI110-.LCFI109
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE70:
.LSFDE72:
	.4byte	.LEFDE72-.LASFDE72
.LASFDE72:
	.4byte	.Lframe0
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.byte	0x4
	.4byte	.LCFI111-.LFB213
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI112-.LCFI111
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI113-.LCFI112
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE72:
.LSFDE74:
	.4byte	.LEFDE74-.LASFDE74
.LASFDE74:
	.4byte	.Lframe0
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.byte	0x4
	.4byte	.LCFI114-.LFB219
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI115-.LCFI114
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
	.4byte	.LCFI116-.LCFI115
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE74:
.LSFDE76:
	.4byte	.LEFDE76-.LASFDE76
.LASFDE76:
	.4byte	.Lframe0
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.byte	0x4
	.4byte	.LCFI117-.LFB222
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI118-.LCFI117
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI119-.LCFI118
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE76:
.LSFDE78:
	.4byte	.LEFDE78-.LASFDE78
.LASFDE78:
	.4byte	.Lframe0
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.byte	0x4
	.4byte	.LCFI120-.LFB224
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI121-.LCFI120
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
	.4byte	.LCFI122-.LCFI121
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI123-.LCFI122
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI124-.LCFI123
	.byte	0xb
	.align	2
.LEFDE78:
.LSFDE80:
	.4byte	.LEFDE80-.LASFDE80
.LASFDE80:
	.4byte	.Lframe0
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.byte	0x4
	.4byte	.LCFI125-.LFB233
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI126-.LCFI125
	.byte	0x83
	.uleb128 0xc
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
	.align	2
.LEFDE80:
.LSFDE82:
	.4byte	.LEFDE82-.LASFDE82
.LASFDE82:
	.4byte	.Lframe0
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.byte	0x4
	.4byte	.LCFI128-.LFB234
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI129-.LCFI128
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI130-.LCFI129
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE82:
.LSFDE84:
	.4byte	.LEFDE84-.LASFDE84
.LASFDE84:
	.4byte	.Lframe0
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.byte	0x4
	.4byte	.LCFI131-.LFB235
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI132-.LCFI131
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
	.4byte	.LCFI133-.LCFI132
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE84:
.LSFDE86:
	.4byte	.LEFDE86-.LASFDE86
.LASFDE86:
	.4byte	.Lframe0
	.4byte	.LFB140
	.4byte	.LFE140-.LFB140
	.byte	0x4
	.4byte	.LCFI134-.LFB140
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI135-.LCFI134
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
	.4byte	.LCFI136-.LCFI135
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI137-.LCFI136
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI138-.LCFI137
	.byte	0xb
	.align	2
.LEFDE86:
.LSFDE88:
	.4byte	.LEFDE88-.LASFDE88
.LASFDE88:
	.4byte	.Lframe0
	.4byte	.LFB141
	.4byte	.LFE141-.LFB141
	.byte	0x4
	.4byte	.LCFI139-.LFB141
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI140-.LCFI139
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
	.4byte	.LCFI141-.LCFI140
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE88:
.LSFDE90:
	.4byte	.LEFDE90-.LASFDE90
.LASFDE90:
	.4byte	.Lframe0
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.byte	0x4
	.4byte	.LCFI142-.LFB236
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI143-.LCFI142
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
	.4byte	.LCFI144-.LCFI143
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI145-.LCFI144
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI146-.LCFI145
	.byte	0xb
	.align	2
.LEFDE90:
.LSFDE92:
	.4byte	.LEFDE92-.LASFDE92
.LASFDE92:
	.4byte	.Lframe0
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.byte	0x4
	.4byte	.LCFI147-.LFB237
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI148-.LCFI147
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
.LEFDE92:
.LSFDE94:
	.4byte	.LEFDE94-.LASFDE94
.LASFDE94:
	.4byte	.Lframe0
	.4byte	.LFB240
	.4byte	.LFE240-.LFB240
	.byte	0x4
	.4byte	.LCFI150-.LFB240
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI151-.LCFI150
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
	.4byte	.LCFI152-.LCFI151
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE94:
.LSFDE96:
	.4byte	.LEFDE96-.LASFDE96
.LASFDE96:
	.4byte	.Lframe0
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.byte	0x4
	.4byte	.LCFI153-.LFB243
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI154-.LCFI153
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
	.4byte	.LCFI155-.LCFI154
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE96:
.LSFDE98:
	.4byte	.LEFDE98-.LASFDE98
.LASFDE98:
	.4byte	.Lframe0
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.byte	0x4
	.4byte	.LCFI156-.LFB244
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI157-.LCFI156
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LCFI158-.LCFI157
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE98:
.LSFDE100:
	.4byte	.LEFDE100-.LASFDE100
.LASFDE100:
	.4byte	.Lframe0
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.byte	0x4
	.4byte	.LCFI159-.LFB245
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI160-.LCFI159
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
	.4byte	.LCFI161-.LCFI160
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE100:
.LSFDE102:
	.4byte	.LEFDE102-.LASFDE102
.LASFDE102:
	.4byte	.Lframe0
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.byte	0x4
	.4byte	.LCFI162-.LFB246
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI163-.LCFI162
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
.LEFDE102:
.LSFDE104:
	.4byte	.LEFDE104-.LASFDE104
.LASFDE104:
	.4byte	.Lframe0
	.4byte	.LFB247
	.4byte	.LFE247-.LFB247
	.byte	0x4
	.4byte	.LCFI165-.LFB247
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI166-.LCFI165
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
	.4byte	.LCFI167-.LCFI166
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE104:
.LSFDE106:
	.4byte	.LEFDE106-.LASFDE106
.LASFDE106:
	.4byte	.Lframe0
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.byte	0x4
	.4byte	.LCFI168-.LFB254
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI169-.LCFI168
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
	.4byte	.LCFI170-.LCFI169
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE106:
.LSFDE108:
	.4byte	.LEFDE108-.LASFDE108
.LASFDE108:
	.4byte	.Lframe0
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.byte	0x4
	.4byte	.LCFI171-.LFB255
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI172-.LCFI171
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
	.4byte	.LCFI173-.LCFI172
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE108:
.LSFDE110:
	.4byte	.LEFDE110-.LASFDE110
.LASFDE110:
	.4byte	.Lframe0
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.byte	0x4
	.4byte	.LCFI174-.LFB256
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI175-.LCFI174
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
	.4byte	.LCFI176-.LCFI175
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE110:
.LSFDE112:
	.4byte	.LEFDE112-.LASFDE112
.LASFDE112:
	.4byte	.Lframe0
	.4byte	.LFB262
	.4byte	.LFE262-.LFB262
	.byte	0x4
	.4byte	.LCFI177-.LFB262
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI178-.LCFI177
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
	.4byte	.LCFI179-.LCFI178
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI180-.LCFI179
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI181-.LCFI180
	.byte	0xb
	.align	2
.LEFDE112:
.LSFDE114:
	.4byte	.LEFDE114-.LASFDE114
.LASFDE114:
	.4byte	.Lframe0
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.byte	0x4
	.4byte	.LCFI182-.LFB170
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI183-.LCFI182
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LCFI184-.LCFI183
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE114:
.LSFDE116:
	.4byte	.LEFDE116-.LASFDE116
.LASFDE116:
	.4byte	.Lframe0
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.byte	0x4
	.4byte	.LCFI185-.LFB176
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI186-.LCFI185
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
	.4byte	.LCFI187-.LCFI186
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI188-.LCFI187
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI189-.LCFI188
	.byte	0xb
	.align	2
.LEFDE116:
.LSFDE118:
	.4byte	.LEFDE118-.LASFDE118
.LASFDE118:
	.4byte	.Lframe0
	.4byte	.LFB263
	.4byte	.LFE263-.LFB263
	.byte	0x4
	.4byte	.LCFI190-.LFB263
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI191-.LCFI190
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
	.4byte	.LCFI192-.LCFI191
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI193-.LCFI192
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI194-.LCFI193
	.byte	0xb
	.align	2
.LEFDE118:
.LSFDE120:
	.4byte	.LEFDE120-.LASFDE120
.LASFDE120:
	.4byte	.Lframe0
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.byte	0x4
	.4byte	.LCFI195-.LFB264
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI196-.LCFI195
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
	.4byte	.LCFI197-.LCFI196
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI198-.LCFI197
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x28
	.byte	0x4
	.4byte	.LCFI199-.LCFI198
	.byte	0xb
	.align	2
.LEFDE120:
.LSFDE122:
	.4byte	.LEFDE122-.LASFDE122
.LASFDE122:
	.4byte	.Lframe0
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.byte	0x4
	.4byte	.LCFI200-.LFB180
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI201-.LCFI200
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
	.4byte	.LCFI202-.LCFI201
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE122:
.LSFDE124:
	.4byte	.LEFDE124-.LASFDE124
.LASFDE124:
	.4byte	.Lframe0
	.4byte	.LFB149
	.4byte	.LFE149-.LFB149
	.byte	0x4
	.4byte	.LCFI203-.LFB149
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI204-.LCFI203
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
	.4byte	.LCFI205-.LCFI204
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI206-.LCFI205
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI207-.LCFI206
	.byte	0xb
	.align	2
.LEFDE124:
.LSFDE126:
	.4byte	.LEFDE126-.LASFDE126
.LASFDE126:
	.4byte	.Lframe0
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.byte	0x4
	.4byte	.LCFI208-.LFB265
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI209-.LCFI208
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
	.4byte	.LCFI210-.LCFI209
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI211-.LCFI210
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x18
	.align	2
.LEFDE126:
.LSFDE128:
	.4byte	.LEFDE128-.LASFDE128
.LASFDE128:
	.4byte	.Lframe0
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.byte	0x4
	.4byte	.LCFI212-.LFB266
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI213-.LCFI212
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
	.4byte	.LCFI214-.LCFI213
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE128:
.LSFDE130:
	.4byte	.LEFDE130-.LASFDE130
.LASFDE130:
	.4byte	.Lframe0
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.byte	0x4
	.4byte	.LCFI215-.LFB267
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI216-.LCFI215
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
	.4byte	.LCFI217-.LCFI216
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE130:
.LSFDE132:
	.4byte	.LEFDE132-.LASFDE132
.LASFDE132:
	.4byte	.Lframe0
	.4byte	.LFB146
	.4byte	.LFE146-.LFB146
	.byte	0x4
	.4byte	.LCFI218-.LFB146
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
.LEFDE132:
.LSFDE134:
	.4byte	.LEFDE134-.LASFDE134
.LASFDE134:
	.4byte	.Lframe0
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.byte	0x4
	.4byte	.LCFI221-.LFB165
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI222-.LCFI221
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
	.4byte	.LCFI223-.LCFI222
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE134:
.LSFDE136:
	.4byte	.LEFDE136-.LASFDE136
.LASFDE136:
	.4byte	.Lframe0
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.byte	0x4
	.4byte	.LCFI224-.LFB193
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI225-.LCFI224
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
	.4byte	.LCFI226-.LCFI225
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE136:
.LSFDE138:
	.4byte	.LEFDE138-.LASFDE138
.LASFDE138:
	.4byte	.Lframe0
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.byte	0x4
	.4byte	.LCFI227-.LFB194
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI228-.LCFI227
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
	.4byte	.LCFI229-.LCFI228
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI230-.LCFI229
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI231-.LCFI230
	.byte	0xb
	.align	2
.LEFDE138:
.LSFDE140:
	.4byte	.LEFDE140-.LASFDE140
.LASFDE140:
	.4byte	.Lframe0
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.byte	0x4
	.4byte	.LCFI232-.LFB197
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI233-.LCFI232
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
	.4byte	.LCFI234-.LCFI233
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE140:
.LSFDE142:
	.4byte	.LEFDE142-.LASFDE142
.LASFDE142:
	.4byte	.Lframe0
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.byte	0x4
	.4byte	.LCFI235-.LFB198
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI236-.LCFI235
	.byte	0x83
	.uleb128 0xc
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
	.align	2
.LEFDE142:
.LSFDE144:
	.4byte	.LEFDE144-.LASFDE144
.LASFDE144:
	.4byte	.Lframe0
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.byte	0x4
	.4byte	.LCFI238-.LFB196
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI239-.LCFI238
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
	.4byte	.LCFI240-.LCFI239
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI241-.LCFI240
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI242-.LCFI241
	.byte	0xb
	.align	2
.LEFDE144:
.LSFDE146:
	.4byte	.LEFDE146-.LASFDE146
.LASFDE146:
	.4byte	.Lframe0
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.byte	0x4
	.4byte	.LCFI243-.LFB199
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI244-.LCFI243
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
	.4byte	.LCFI245-.LCFI244
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE146:
.LSFDE148:
	.4byte	.LEFDE148-.LASFDE148
.LASFDE148:
	.4byte	.Lframe0
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.byte	0x4
	.4byte	.LCFI246-.LFB202
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI247-.LCFI246
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LCFI248-.LCFI247
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE148:
.LSFDE150:
	.4byte	.LEFDE150-.LASFDE150
.LASFDE150:
	.4byte	.Lframe0
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.byte	0x4
	.4byte	.LCFI249-.LFB220
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI250-.LCFI249
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
	.4byte	.LCFI251-.LCFI250
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI252-.LCFI251
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x24
	.align	2
.LEFDE150:
.LSFDE152:
	.4byte	.LEFDE152-.LASFDE152
.LASFDE152:
	.4byte	.Lframe0
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.byte	0x4
	.4byte	.LCFI253-.LFB231
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI254-.LCFI253
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
	.4byte	.LCFI255-.LCFI254
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI256-.LCFI255
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x20
	.byte	0x4
	.4byte	.LCFI257-.LCFI256
	.byte	0xb
	.align	2
.LEFDE152:
.LSFDE154:
	.4byte	.LEFDE154-.LASFDE154
.LASFDE154:
	.4byte	.Lframe0
	.4byte	.LFB258
	.4byte	.LFE258-.LFB258
	.byte	0x4
	.4byte	.LCFI258-.LFB258
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI259-.LCFI258
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
	.4byte	.LCFI260-.LCFI259
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI261-.LCFI260
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x18
	.byte	0x4
	.4byte	.LCFI262-.LCFI261
	.byte	0xb
	.align	2
.LEFDE154:
.LSFDE156:
	.4byte	.LEFDE156-.LASFDE156
.LASFDE156:
	.4byte	.Lframe0
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.byte	0x4
	.4byte	.LCFI263-.LFB230
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI264-.LCFI263
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
	.4byte	.LCFI265-.LCFI264
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI266-.LCFI265
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI267-.LCFI266
	.byte	0xb
	.align	2
.LEFDE156:
.LSFDE158:
	.4byte	.LEFDE158-.LASFDE158
.LASFDE158:
	.4byte	.Lframe0
	.4byte	.LFB207
	.4byte	.LFE207-.LFB207
	.byte	0x4
	.4byte	.LCFI268-.LFB207
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI269-.LCFI268
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
	.4byte	.LCFI270-.LCFI269
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE158:
.LSFDE160:
	.4byte	.LEFDE160-.LASFDE160
.LASFDE160:
	.4byte	.Lframe0
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.byte	0x4
	.4byte	.LCFI271-.LFB210
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI272-.LCFI271
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
	.4byte	.LCFI273-.LCFI272
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI274-.LCFI273
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI275-.LCFI274
	.byte	0xb
	.align	2
.LEFDE160:
.LSFDE162:
	.4byte	.LEFDE162-.LASFDE162
.LASFDE162:
	.4byte	.Lframe0
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.byte	0x4
	.4byte	.LCFI276-.LFB215
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI277-.LCFI276
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
	.4byte	.LCFI278-.LCFI277
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI279-.LCFI278
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI280-.LCFI279
	.byte	0xb
	.align	2
.LEFDE162:
.LSFDE164:
	.4byte	.LEFDE164-.LASFDE164
.LASFDE164:
	.4byte	.Lframe0
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.byte	0x4
	.4byte	.LCFI281-.LFB216
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
.LEFDE164:
.LSFDE166:
	.4byte	.LEFDE166-.LASFDE166
.LASFDE166:
	.4byte	.Lframe0
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.byte	0x4
	.4byte	.LCFI284-.LFB217
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI285-.LCFI284
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
	.4byte	.LCFI286-.LCFI285
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE166:
.LSFDE168:
	.4byte	.LEFDE168-.LASFDE168
.LASFDE168:
	.4byte	.Lframe0
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.byte	0x4
	.4byte	.LCFI287-.LFB218
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI288-.LCFI287
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
	.align	2
.LEFDE168:
.LSFDE170:
	.4byte	.LEFDE170-.LASFDE170
.LASFDE170:
	.4byte	.Lframe0
	.4byte	.LFB147
	.4byte	.LFE147-.LFB147
	.byte	0x4
	.4byte	.LCFI290-.LFB147
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI291-.LCFI290
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
	.4byte	.LCFI292-.LCFI291
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE170:
.LSFDE172:
	.4byte	.LEFDE172-.LASFDE172
.LASFDE172:
	.4byte	.Lframe0
	.4byte	.LFB225
	.4byte	.LFE225-.LFB225
	.byte	0x4
	.4byte	.LCFI293-.LFB225
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI294-.LCFI293
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
	.4byte	.LCFI295-.LCFI294
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI296-.LCFI295
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE172:
.LSFDE174:
	.4byte	.LEFDE174-.LASFDE174
.LASFDE174:
	.4byte	.Lframe0
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.byte	0x4
	.4byte	.LCFI297-.LFB249
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI298-.LCFI297
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
	.4byte	.LCFI299-.LCFI298
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE174:
.LSFDE176:
	.4byte	.LEFDE176-.LASFDE176
.LASFDE176:
	.4byte	.Lframe0
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.byte	0x4
	.4byte	.LCFI300-.LFB250
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI301-.LCFI300
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
	.4byte	.LCFI302-.LCFI301
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI303-.LCFI302
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE176:
.LSFDE178:
	.4byte	.LEFDE178-.LASFDE178
.LASFDE178:
	.4byte	.Lframe0
	.4byte	.LFB268
	.4byte	.LFE268-.LFB268
	.byte	0x4
	.4byte	.LCFI304-.LFB268
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI305-.LCFI304
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
	.align	2
.LEFDE178:
.LSFDE180:
	.4byte	.LEFDE180-.LASFDE180
.LASFDE180:
	.4byte	.Lframe0
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.byte	0x4
	.4byte	.LCFI307-.LFB166
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI308-.LCFI307
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
	.4byte	.LCFI309-.LCFI308
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI310-.LCFI309
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE180:
.LSFDE182:
	.4byte	.LEFDE182-.LASFDE182
.LASFDE182:
	.4byte	.Lframe0
	.4byte	.LFB238
	.4byte	.LFE238-.LFB238
	.byte	0x4
	.4byte	.LCFI311-.LFB238
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI312-.LCFI311
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
	.4byte	.LCFI313-.LCFI312
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI314-.LCFI313
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE182:
.LSFDE184:
	.4byte	.LEFDE184-.LASFDE184
.LASFDE184:
	.4byte	.Lframe0
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.byte	0x4
	.4byte	.LCFI315-.LFB257
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI316-.LCFI315
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
	.4byte	.LCFI317-.LCFI316
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI318-.LCFI317
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI319-.LCFI318
	.byte	0xb
	.align	2
.LEFDE184:
.LSFDE186:
	.4byte	.LEFDE186-.LASFDE186
.LASFDE186:
	.4byte	.Lframe0
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.byte	0x4
	.4byte	.LCFI320-.LFB241
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI321-.LCFI320
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
	.4byte	.LCFI322-.LCFI321
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE186:
.LSFDE188:
	.4byte	.LEFDE188-.LASFDE188
.LASFDE188:
	.4byte	.Lframe0
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.byte	0x4
	.4byte	.LCFI323-.LFB242
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI324-.LCFI323
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
	.4byte	.LCFI325-.LCFI324
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE188:
.LSFDE190:
	.4byte	.LEFDE190-.LASFDE190
.LASFDE190:
	.4byte	.Lframe0
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.byte	0x4
	.4byte	.LCFI326-.LFB208
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI327-.LCFI326
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
	.4byte	.LCFI328-.LCFI327
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI329-.LCFI328
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI330-.LCFI329
	.byte	0xb
	.align	2
.LEFDE190:
.LSFDE192:
	.4byte	.LEFDE192-.LASFDE192
.LASFDE192:
	.4byte	.Lframe0
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.byte	0x4
	.4byte	.LCFI331-.LFB201
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI332-.LCFI331
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
	.4byte	.LCFI333-.LCFI332
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE192:
.LSFDE194:
	.4byte	.LEFDE194-.LASFDE194
.LASFDE194:
	.4byte	.Lframe0
	.4byte	.LFB221
	.4byte	.LFE221-.LFB221
	.byte	0x4
	.4byte	.LCFI334-.LFB221
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI335-.LCFI334
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
	.4byte	.LCFI336-.LCFI335
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI337-.LCFI336
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI338-.LCFI337
	.byte	0xb
	.align	2
.LEFDE194:
.LSFDE196:
	.4byte	.LEFDE196-.LASFDE196
.LASFDE196:
	.4byte	.Lframe0
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.byte	0x4
	.4byte	.LCFI339-.LFB228
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI340-.LCFI339
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
	.4byte	.LCFI341-.LCFI340
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI342-.LCFI341
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x20
	.byte	0x4
	.4byte	.LCFI343-.LCFI342
	.byte	0xb
	.align	2
.LEFDE196:
.LSFDE198:
	.4byte	.LEFDE198-.LASFDE198
.LASFDE198:
	.4byte	.Lframe0
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.byte	0x4
	.4byte	.LCFI344-.LFB169
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI345-.LCFI344
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
	.4byte	.LCFI346-.LCFI345
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE198:
.LSFDE200:
	.4byte	.LEFDE200-.LASFDE200
.LASFDE200:
	.4byte	.Lframe0
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.byte	0x4
	.4byte	.LCFI347-.LFB171
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI348-.LCFI347
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
	.4byte	.LCFI349-.LCFI348
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI350-.LCFI349
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE200:
.LSFDE202:
	.4byte	.LEFDE202-.LASFDE202
.LASFDE202:
	.4byte	.Lframe0
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.byte	0x4
	.4byte	.LCFI351-.LFB203
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI352-.LCFI351
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
	.4byte	.LCFI353-.LCFI352
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI354-.LCFI353
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI355-.LCFI354
	.byte	0xb
	.align	2
.LEFDE202:
.LSFDE204:
	.4byte	.LEFDE204-.LASFDE204
.LASFDE204:
	.4byte	.Lframe0
	.4byte	.LFB226
	.4byte	.LFE226-.LFB226
	.byte	0x4
	.4byte	.LCFI356-.LFB226
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI357-.LCFI356
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI358-.LCFI357
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE204:
.LSFDE206:
	.4byte	.LEFDE206-.LASFDE206
.LASFDE206:
	.4byte	.Lframe0
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.byte	0x4
	.4byte	.LCFI359-.LFB205
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI360-.LCFI359
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
.LEFDE206:
.LSFDE208:
	.4byte	.LEFDE208-.LASFDE208
.LASFDE208:
	.4byte	.Lframe0
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.byte	0x4
	.4byte	.LCFI362-.LFB204
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI363-.LCFI362
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
	.4byte	.LCFI364-.LCFI363
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI365-.LCFI364
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI366-.LCFI365
	.byte	0xb
	.align	2
.LEFDE208:
.LSFDE210:
	.4byte	.LEFDE210-.LASFDE210
.LASFDE210:
	.4byte	.Lframe0
	.4byte	.LFB211
	.4byte	.LFE211-.LFB211
	.byte	0x4
	.4byte	.LCFI367-.LFB211
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI368-.LCFI367
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
	.4byte	.LCFI369-.LCFI368
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE210:
.LSFDE212:
	.4byte	.LEFDE212-.LASFDE212
.LASFDE212:
	.4byte	.Lframe0
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.byte	0x4
	.4byte	.LCFI370-.LFB227
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI371-.LCFI370
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
	.4byte	.LCFI372-.LCFI371
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE212:
.LSFDE214:
	.4byte	.LEFDE214-.LASFDE214
.LASFDE214:
	.4byte	.Lframe0
	.4byte	.LFB209
	.4byte	.LFE209-.LFB209
	.byte	0x4
	.4byte	.LCFI373-.LFB209
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI374-.LCFI373
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LCFI375-.LCFI374
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE214:
.LSFDE216:
	.4byte	.LEFDE216-.LASFDE216
.LASFDE216:
	.4byte	.Lframe0
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.byte	0x4
	.4byte	.LCFI376-.LFB229
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI377-.LCFI376
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
	.4byte	.LCFI378-.LCFI377
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI379-.LCFI378
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x28
	.byte	0x4
	.4byte	.LCFI380-.LCFI379
	.byte	0xb
	.align	2
.LEFDE216:
.LSFDE218:
	.4byte	.LEFDE218-.LASFDE218
.LASFDE218:
	.4byte	.Lframe0
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.byte	0x4
	.4byte	.LCFI381-.LFB239
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI382-.LCFI381
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
	.4byte	.LCFI383-.LCFI382
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE218:
.LSFDE220:
	.4byte	.LEFDE220-.LASFDE220
.LASFDE220:
	.4byte	.Lframe0
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.byte	0x4
	.4byte	.LCFI384-.LFB155
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI385-.LCFI384
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
	.4byte	.LCFI386-.LCFI385
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI387-.LCFI386
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.align	2
.LEFDE220:
.LSFDE222:
	.4byte	.LEFDE222-.LASFDE222
.LASFDE222:
	.4byte	.Lframe0
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.byte	0x4
	.4byte	.LCFI388-.LFB156
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI389-.LCFI388
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
	.4byte	.LCFI390-.LCFI389
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE222:
.LSFDE224:
	.4byte	.LEFDE224-.LASFDE224
.LASFDE224:
	.4byte	.Lframe0
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.byte	0x4
	.4byte	.LCFI391-.LFB152
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI392-.LCFI391
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI393-.LCFI392
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE224:
.LSFDE226:
	.4byte	.LEFDE226-.LASFDE226
.LASFDE226:
	.4byte	.Lframe0
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.byte	0x4
	.4byte	.LCFI394-.LFB153
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI395-.LCFI394
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI396-.LCFI395
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE226:
.LSFDE228:
	.4byte	.LEFDE228-.LASFDE228
.LASFDE228:
	.4byte	.Lframe0
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.byte	0x4
	.4byte	.LCFI397-.LFB158
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI398-.LCFI397
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
	.4byte	.LCFI399-.LCFI398
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI400-.LCFI399
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x24
	.byte	0x4
	.4byte	.LCFI401-.LCFI400
	.byte	0xb
	.align	2
.LEFDE228:
.LSFDE230:
	.4byte	.LEFDE230-.LASFDE230
.LASFDE230:
	.4byte	.Lframe0
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.byte	0x4
	.4byte	.LCFI402-.LFB248
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI403-.LCFI402
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
	.4byte	.LCFI404-.LCFI403
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI405-.LCFI404
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI406-.LCFI405
	.byte	0xb
	.align	2
.LEFDE230:
.LSFDE232:
	.4byte	.LEFDE232-.LASFDE232
.LASFDE232:
	.4byte	.Lframe0
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.byte	0x4
	.4byte	.LCFI407-.LFB251
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI408-.LCFI407
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
	.4byte	.LCFI409-.LCFI408
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE232:
.LSFDE234:
	.4byte	.LEFDE234-.LASFDE234
.LASFDE234:
	.4byte	.Lframe0
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.byte	0x4
	.4byte	.LCFI410-.LFB223
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI411-.LCFI410
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
	.4byte	.LCFI412-.LCFI411
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE234:
.LSFDE236:
	.4byte	.LEFDE236-.LASFDE236
.LASFDE236:
	.4byte	.Lframe0
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.byte	0x4
	.4byte	.LCFI413-.LFB253
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI414-.LCFI413
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
	.4byte	.LCFI415-.LCFI414
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE236:
.LSFDE238:
	.4byte	.LEFDE238-.LASFDE238
.LASFDE238:
	.4byte	.Lframe0
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.byte	0x4
	.4byte	.LCFI416-.LFB252
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI417-.LCFI416
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
	.4byte	.LCFI418-.LCFI417
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE238:
.LSFDE240:
	.4byte	.LEFDE240-.LASFDE240
.LASFDE240:
	.4byte	.Lframe0
	.4byte	.LFB259
	.4byte	.LFE259-.LFB259
	.byte	0x4
	.4byte	.LCFI419-.LFB259
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI420-.LCFI419
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
	.4byte	.LCFI421-.LCFI420
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI422-.LCFI421
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI423-.LCFI422
	.byte	0xb
	.align	2
.LEFDE240:
.LSFDE242:
	.4byte	.LEFDE242-.LASFDE242
.LASFDE242:
	.4byte	.Lframe0
	.4byte	.LFB260
	.4byte	.LFE260-.LFB260
	.byte	0x4
	.4byte	.LCFI424-.LFB260
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI425-.LCFI424
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI426-.LCFI425
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE242:
.LSFDE244:
	.4byte	.LEFDE244-.LASFDE244
.LASFDE244:
	.4byte	.Lframe0
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.byte	0x4
	.4byte	.LCFI427-.LFB154
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI428-.LCFI427
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
	.4byte	.LCFI429-.LCFI428
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI430-.LCFI429
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI431-.LCFI430
	.byte	0xb
	.align	2
.LEFDE244:
.LSFDE246:
	.4byte	.LEFDE246-.LASFDE246
.LASFDE246:
	.4byte	.Lframe0
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.byte	0x4
	.4byte	.LCFI432-.LFB159
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI433-.LCFI432
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI434-.LCFI433
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE246:
.LSFDE248:
	.4byte	.LEFDE248-.LASFDE248
.LASFDE248:
	.4byte	.Lframe0
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.byte	0x4
	.4byte	.LCFI435-.LFB157
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI436-.LCFI435
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
	.4byte	.LCFI437-.LCFI436
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.byte	0x4
	.4byte	.LCFI438-.LCFI437
	.byte	0xa
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x2c
	.byte	0x4
	.4byte	.LCFI439-.LCFI438
	.byte	0xb
	.align	2
.LEFDE248:
.LSFDE250:
	.4byte	.LEFDE250-.LASFDE250
.LASFDE250:
	.4byte	.Lframe0
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.byte	0x4
	.4byte	.LCFI440-.LFB160
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI441-.LCFI440
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
.LEFDE250:
.LSFDE252:
	.4byte	.LEFDE252-.LASFDE252
.LASFDE252:
	.4byte	.Lframe0
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.byte	0x4
	.4byte	.LCFI443-.LFB214
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI444-.LCFI443
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
	.4byte	.LCFI445-.LCFI444
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE252:
.LSFDE254:
	.4byte	.LEFDE254-.LASFDE254
.LASFDE254:
	.4byte	.Lframe0
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.byte	0x4
	.4byte	.LCFI446-.LFB232
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI447-.LCFI446
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
	.4byte	.LCFI448-.LCFI447
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE254:
.LSFDE256:
	.4byte	.LEFDE256-.LASFDE256
.LASFDE256:
	.4byte	.Lframe0
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.byte	0x4
	.4byte	.LCFI449-.LFB150
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI450-.LCFI449
	.byte	0x83
	.uleb128 0xc
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
	.4byte	.LCFI451-.LCFI450
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE256:
.LSFDE258:
	.4byte	.LEFDE258-.LASFDE258
.LASFDE258:
	.4byte	.Lframe0
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.byte	0x4
	.4byte	.LCFI452-.LFB151
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI453-.LCFI452
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
	.4byte	.LCFI454-.LCFI453
	.byte	0xc
	.uleb128 0xb
	.uleb128 0x4
	.align	2
.LEFDE258:
.LSFDE260:
	.4byte	.LEFDE260-.LASFDE260
.LASFDE260:
	.4byte	.Lframe0
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.byte	0x4
	.4byte	.LCFI455-.LFB269
	.byte	0xd
	.uleb128 0xc
	.byte	0x4
	.4byte	.LCFI456-.LCFI455
	.byte	0x8b
	.uleb128 0x4
	.byte	0x8d
	.uleb128 0x3
	.byte	0x8e
	.uleb128 0x2
	.byte	0x4
	.4byte	.LCFI457-.LCFI456
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
	.file 10 "./arch/arm/include/asm/barrier.h"
	.file 11 "include/linux/init.h"
	.file 12 "include/linux/printk.h"
	.file 13 "include/linux/kernel.h"
	.file 14 "drivers/rkflash/rksftl/sftl_inc.h"
	.file 15 "drivers/rkflash/rksftl/flash_com.h"
	.file 16 "drivers/rkflash/rksftl/sftl_struct.h"
	.file 17 "drivers/rkflash/rksftl/sftl_global.h"
	.file 18 "include/linux/string.h"
	.file 19 "./arch/arm/include/asm/string.h"
	.file 20 "drivers/rkflash/rksftl/typedef.h"
	.file 21 "<built-in>"
	.section	.debug_info,"",%progbits
.Ldebug_info0:
	.4byte	0x676b
	.2byte	0x4
	.4byte	.Ldebug_abbrev0
	.byte	0x4
	.uleb128 0x1
	.4byte	.LASF665
	.byte	0x1
	.4byte	.LASF666
	.4byte	.LASF667
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
	.uleb128 0xa
	.uleb128 0x9
	.4byte	.LASF17
	.byte	0xa
	.byte	0x28
	.4byte	0xfb
	.uleb128 0x8
	.byte	0x4
	.4byte	0xef
	.uleb128 0x7
	.4byte	.LASF18
	.byte	0xb
	.byte	0x83
	.4byte	0x10c
	.uleb128 0x8
	.byte	0x4
	.4byte	0x112
	.uleb128 0xb
	.4byte	0x64
	.uleb128 0xc
	.4byte	0x101
	.4byte	0x122
	.uleb128 0xd
	.byte	0
	.uleb128 0x9
	.4byte	.LASF19
	.byte	0xb
	.byte	0x86
	.4byte	0x117
	.uleb128 0x9
	.4byte	.LASF20
	.byte	0xb
	.byte	0x86
	.4byte	0x117
	.uleb128 0x9
	.4byte	.LASF21
	.byte	0xb
	.byte	0x87
	.4byte	0x117
	.uleb128 0x9
	.4byte	.LASF22
	.byte	0xb
	.byte	0x87
	.4byte	0x117
	.uleb128 0xc
	.4byte	0x35
	.4byte	0x159
	.uleb128 0xd
	.byte	0
	.uleb128 0x9
	.4byte	.LASF23
	.byte	0xb
	.byte	0x8e
	.4byte	0x14e
	.uleb128 0x9
	.4byte	.LASF24
	.byte	0xb
	.byte	0x8f
	.4byte	0xb6
	.uleb128 0x9
	.4byte	.LASF25
	.byte	0xb
	.byte	0x90
	.4byte	0x41
	.uleb128 0x9
	.4byte	.LASF26
	.byte	0xb
	.byte	0x9c
	.4byte	0xfb
	.uleb128 0x9
	.4byte	.LASF27
	.byte	0xb
	.byte	0x9e
	.4byte	0xbc
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x19b
	.uleb128 0xd
	.byte	0
	.uleb128 0x4
	.4byte	0x190
	.uleb128 0x9
	.4byte	.LASF28
	.byte	0xc
	.byte	0xa
	.4byte	0x19b
	.uleb128 0x9
	.4byte	.LASF29
	.byte	0xc
	.byte	0xb
	.4byte	0x19b
	.uleb128 0xc
	.4byte	0x64
	.4byte	0x1c1
	.uleb128 0xd
	.byte	0
	.uleb128 0x9
	.4byte	.LASF30
	.byte	0xc
	.byte	0x2e
	.4byte	0x1b6
	.uleb128 0x9
	.4byte	.LASF31
	.byte	0xc
	.byte	0x9f
	.4byte	0x64
	.uleb128 0x9
	.4byte	.LASF32
	.byte	0xc
	.byte	0xa0
	.4byte	0x64
	.uleb128 0x9
	.4byte	.LASF33
	.byte	0xc
	.byte	0xa1
	.4byte	0x64
	.uleb128 0xe
	.4byte	.LASF35
	.uleb128 0x4
	.4byte	0x1ed
	.uleb128 0xf
	.4byte	.LASF34
	.byte	0xc
	.2byte	0x1a4
	.4byte	0x1f2
	.uleb128 0xe
	.4byte	.LASF36
	.uleb128 0xf
	.4byte	.LASF37
	.byte	0xd
	.2byte	0x104
	.4byte	0x203
	.uleb128 0x10
	.4byte	0xa4
	.4byte	0x223
	.uleb128 0x11
	.4byte	0x64
	.byte	0
	.uleb128 0xf
	.4byte	.LASF38
	.byte	0xd
	.2byte	0x105
	.4byte	0x22f
	.uleb128 0x8
	.byte	0x4
	.4byte	0x214
	.uleb128 0xf
	.4byte	.LASF39
	.byte	0xd
	.2byte	0x1bf
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF40
	.byte	0xd
	.2byte	0x1c0
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF41
	.byte	0xd
	.2byte	0x1c1
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF42
	.byte	0xd
	.2byte	0x1c2
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF43
	.byte	0xd
	.2byte	0x1c3
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF44
	.byte	0xd
	.2byte	0x1c4
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF45
	.byte	0xd
	.2byte	0x1c5
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF46
	.byte	0xd
	.2byte	0x1c7
	.4byte	0xbc
	.uleb128 0xf
	.4byte	.LASF47
	.byte	0xd
	.2byte	0x1da
	.4byte	0x64
	.uleb128 0xf
	.4byte	.LASF48
	.byte	0xd
	.2byte	0x1dc
	.4byte	0xbc
	.uleb128 0x12
	.4byte	.LASF668
	.byte	0x4
	.4byte	0x41
	.byte	0xd
	.2byte	0x1df
	.4byte	0x2dd
	.uleb128 0x13
	.4byte	.LASF49
	.byte	0
	.uleb128 0x13
	.4byte	.LASF50
	.byte	0x1
	.uleb128 0x13
	.4byte	.LASF51
	.byte	0x2
	.uleb128 0x13
	.4byte	.LASF52
	.byte	0x3
	.uleb128 0x13
	.4byte	.LASF53
	.byte	0x4
	.byte	0
	.uleb128 0xf
	.4byte	.LASF54
	.byte	0xd
	.2byte	0x1e5
	.4byte	0x2ad
	.uleb128 0xf
	.4byte	.LASF55
	.byte	0xd
	.2byte	0x1f8
	.4byte	0x19b
	.uleb128 0xf
	.4byte	.LASF56
	.byte	0xd
	.2byte	0x203
	.4byte	0x19b
	.uleb128 0x7
	.4byte	.LASF57
	.byte	0xe
	.byte	0x9
	.4byte	0x4f
	.uleb128 0x7
	.4byte	.LASF58
	.byte	0xe
	.byte	0xa
	.4byte	0x5d
	.uleb128 0x7
	.4byte	.LASF59
	.byte	0xe
	.byte	0xb
	.4byte	0x41
	.uleb128 0x7
	.4byte	.LASF60
	.byte	0xe
	.byte	0xc
	.4byte	0x64
	.uleb128 0x7
	.4byte	.LASF61
	.byte	0xe
	.byte	0xd
	.4byte	0x56
	.uleb128 0x14
	.4byte	.LASF66
	.byte	0x14
	.byte	0xf
	.byte	0x22
	.4byte	0x381
	.uleb128 0x15
	.4byte	.LASF62
	.byte	0xf
	.byte	0x23
	.4byte	0x99
	.byte	0
	.uleb128 0x15
	.4byte	.LASF63
	.byte	0xf
	.byte	0x24
	.4byte	0x99
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF64
	.byte	0xf
	.byte	0x25
	.4byte	0x381
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF65
	.byte	0xf
	.byte	0x26
	.4byte	0x381
	.byte	0xc
	.uleb128 0x16
	.ascii	"lpa\000"
	.byte	0xf
	.byte	0x27
	.4byte	0x99
	.byte	0x10
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x99
	.uleb128 0x14
	.4byte	.LASF67
	.byte	0x18
	.byte	0xf
	.byte	0x2a
	.4byte	0x424
	.uleb128 0x15
	.4byte	.LASF68
	.byte	0xf
	.byte	0x2b
	.4byte	0x83
	.byte	0
	.uleb128 0x15
	.4byte	.LASF69
	.byte	0xf
	.byte	0x2c
	.4byte	0x83
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF70
	.byte	0xf
	.byte	0x2d
	.4byte	0x83
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF71
	.byte	0xf
	.byte	0x2e
	.4byte	0x83
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF72
	.byte	0xf
	.byte	0x2f
	.4byte	0x83
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF73
	.byte	0xf
	.byte	0x30
	.4byte	0x83
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF74
	.byte	0xf
	.byte	0x31
	.4byte	0x83
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF75
	.byte	0xf
	.byte	0x32
	.4byte	0x83
	.byte	0xe
	.uleb128 0x15
	.4byte	.LASF76
	.byte	0xf
	.byte	0x33
	.4byte	0x83
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF77
	.byte	0xf
	.byte	0x34
	.4byte	0x83
	.byte	0x12
	.uleb128 0x15
	.4byte	.LASF78
	.byte	0xf
	.byte	0x35
	.4byte	0x83
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF79
	.byte	0xf
	.byte	0x36
	.4byte	0x79
	.byte	0x16
	.byte	0
	.uleb128 0x4
	.4byte	0x387
	.uleb128 0x14
	.4byte	.LASF80
	.byte	0x10
	.byte	0xf
	.byte	0x39
	.4byte	0x466
	.uleb128 0x15
	.4byte	.LASF81
	.byte	0xf
	.byte	0x3a
	.4byte	0x480
	.byte	0
	.uleb128 0x15
	.4byte	.LASF82
	.byte	0xf
	.byte	0x3b
	.4byte	0x49a
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF83
	.byte	0xf
	.byte	0x3c
	.4byte	0x4be
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF84
	.byte	0xf
	.byte	0x3d
	.4byte	0x4be
	.byte	0xc
	.byte	0
	.uleb128 0x10
	.4byte	0x8e
	.4byte	0x47a
	.uleb128 0x11
	.4byte	0x47a
	.uleb128 0x11
	.4byte	0x99
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x83
	.uleb128 0x8
	.byte	0x4
	.4byte	0x466
	.uleb128 0x10
	.4byte	0x99
	.4byte	0x49a
	.uleb128 0x11
	.4byte	0x79
	.uleb128 0x11
	.4byte	0x99
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x486
	.uleb128 0x10
	.4byte	0x99
	.4byte	0x4be
	.uleb128 0x11
	.4byte	0x79
	.uleb128 0x11
	.4byte	0x99
	.uleb128 0x11
	.4byte	0x381
	.uleb128 0x11
	.4byte	0x381
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x4a0
	.uleb128 0x14
	.4byte	.LASF85
	.byte	0x3c
	.byte	0x10
	.byte	0x2b
	.4byte	0x524
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0x2d
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0x10
	.byte	0x2e
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0x10
	.byte	0x2f
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x16
	.ascii	"cnt\000"
	.byte	0x10
	.byte	0x30
	.4byte	0x30c
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x31
	.4byte	0x317
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF89
	.byte	0x10
	.byte	0x32
	.4byte	0x524
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF90
	.byte	0x10
	.byte	0x33
	.4byte	0x534
	.byte	0x1c
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x534
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x7
	.byte	0
	.uleb128 0xc
	.4byte	0x544
	.4byte	0x544
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x7
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x317
	.uleb128 0x14
	.4byte	.LASF91
	.byte	0xc
	.byte	0x10
	.byte	0x3f
	.4byte	0x592
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0x41
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0x10
	.byte	0x42
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0x10
	.byte	0x43
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF92
	.byte	0x10
	.byte	0x44
	.4byte	0x30c
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x45
	.4byte	0x317
	.byte	0x8
	.byte	0
	.uleb128 0x14
	.4byte	.LASF93
	.byte	0x28
	.byte	0x10
	.byte	0x48
	.4byte	0x63b
	.uleb128 0x15
	.4byte	.LASF94
	.byte	0x10
	.byte	0x4a
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF86
	.byte	0x10
	.byte	0x4b
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0x10
	.byte	0x4d
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF96
	.byte	0x10
	.byte	0x4e
	.4byte	0x30c
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF97
	.byte	0x10
	.byte	0x50
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF98
	.byte	0x10
	.byte	0x51
	.4byte	0x30c
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF99
	.byte	0x10
	.byte	0x53
	.4byte	0x63b
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF100
	.byte	0x10
	.byte	0x54
	.4byte	0x544
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF101
	.byte	0x10
	.byte	0x55
	.4byte	0x63b
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF102
	.byte	0x10
	.byte	0x56
	.4byte	0x544
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x58
	.4byte	0x317
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF103
	.byte	0x10
	.byte	0x59
	.4byte	0x317
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF104
	.byte	0x10
	.byte	0x5a
	.4byte	0x317
	.byte	0x24
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x30c
	.uleb128 0x14
	.4byte	.LASF105
	.byte	0xc
	.byte	0x10
	.byte	0x5d
	.4byte	0x67e
	.uleb128 0x15
	.4byte	.LASF106
	.byte	0x10
	.byte	0x5f
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF107
	.byte	0x10
	.byte	0x60
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x16
	.ascii	"hit\000"
	.byte	0x10
	.byte	0x61
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF108
	.byte	0x10
	.byte	0x62
	.4byte	0x544
	.byte	0x8
	.byte	0
	.uleb128 0x14
	.4byte	.LASF109
	.byte	0x24
	.byte	0x10
	.byte	0x65
	.4byte	0x6eb
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0x10
	.byte	0x67
	.4byte	0x317
	.byte	0
	.uleb128 0x15
	.4byte	.LASF110
	.byte	0x10
	.byte	0x68
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x69
	.4byte	0x317
	.byte	0x8
	.uleb128 0x16
	.ascii	"len\000"
	.byte	0x10
	.byte	0x6a
	.4byte	0x317
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF111
	.byte	0x10
	.byte	0x6b
	.4byte	0x317
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF112
	.byte	0x10
	.byte	0x6c
	.4byte	0x317
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF113
	.byte	0x10
	.byte	0x6d
	.4byte	0x6eb
	.byte	0x18
	.uleb128 0x16
	.ascii	"tbl\000"
	.byte	0x10
	.byte	0x6e
	.4byte	0x6fb
	.byte	0x20
	.byte	0
	.uleb128 0xc
	.4byte	0x317
	.4byte	0x6fb
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x70b
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1
	.byte	0
	.uleb128 0x14
	.4byte	.LASF114
	.byte	0x10
	.byte	0x10
	.byte	0x72
	.4byte	0x76b
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0x10
	.byte	0x74
	.4byte	0x30c
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0x75
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x76
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF87
	.byte	0x10
	.byte	0x77
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF115
	.byte	0x10
	.byte	0x78
	.4byte	0x30c
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF116
	.byte	0x10
	.byte	0x79
	.4byte	0x30c
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF113
	.byte	0x10
	.byte	0x7a
	.4byte	0x76b
	.byte	0xe
	.byte	0
	.uleb128 0xc
	.4byte	0x301
	.4byte	0x77b
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1
	.byte	0
	.uleb128 0x14
	.4byte	.LASF117
	.byte	0x10
	.byte	0x10
	.byte	0x7d
	.4byte	0x7c3
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0x10
	.byte	0x7f
	.4byte	0x30c
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0x80
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x81
	.4byte	0x317
	.byte	0x4
	.uleb128 0x16
	.ascii	"lpa\000"
	.byte	0x10
	.byte	0x82
	.4byte	0x317
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF118
	.byte	0x10
	.byte	0x83
	.4byte	0x317
	.byte	0xc
	.byte	0
	.uleb128 0x14
	.4byte	.LASF119
	.byte	0x10
	.byte	0x10
	.byte	0x8f
	.4byte	0x80b
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0x10
	.byte	0x91
	.4byte	0x30c
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0x92
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x93
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF120
	.byte	0x10
	.byte	0x94
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF113
	.byte	0x10
	.byte	0x95
	.4byte	0x80b
	.byte	0xa
	.byte	0
	.uleb128 0xc
	.4byte	0x301
	.4byte	0x81b
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x5
	.byte	0
	.uleb128 0x14
	.4byte	.LASF121
	.byte	0x10
	.byte	0x10
	.byte	0x98
	.4byte	0x863
	.uleb128 0x15
	.4byte	.LASF95
	.byte	0x10
	.byte	0x9a
	.4byte	0x30c
	.byte	0
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0x9b
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF88
	.byte	0x10
	.byte	0x9c
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF122
	.byte	0x10
	.byte	0x9d
	.4byte	0x317
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF123
	.byte	0x10
	.byte	0x9e
	.4byte	0x317
	.byte	0xc
	.byte	0
	.uleb128 0x14
	.4byte	.LASF124
	.byte	0x30
	.byte	0x10
	.byte	0xa1
	.4byte	0x960
	.uleb128 0x16
	.ascii	"tag\000"
	.byte	0x10
	.byte	0xa3
	.4byte	0x317
	.byte	0
	.uleb128 0x16
	.ascii	"ver\000"
	.byte	0x10
	.byte	0xa4
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF92
	.byte	0x10
	.byte	0xa6
	.4byte	0x30c
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF125
	.byte	0x10
	.byte	0xa7
	.4byte	0x301
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF126
	.byte	0x10
	.byte	0xa8
	.4byte	0x301
	.byte	0xb
	.uleb128 0x15
	.4byte	.LASF127
	.byte	0x10
	.byte	0xa9
	.4byte	0x301
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF128
	.byte	0x10
	.byte	0xaa
	.4byte	0x301
	.byte	0xd
	.uleb128 0x15
	.4byte	.LASF129
	.byte	0x10
	.byte	0xab
	.4byte	0x30c
	.byte	0xe
	.uleb128 0x15
	.4byte	.LASF130
	.byte	0x10
	.byte	0xad
	.4byte	0x30c
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF131
	.byte	0x10
	.byte	0xae
	.4byte	0x30c
	.byte	0x12
	.uleb128 0x15
	.4byte	.LASF132
	.byte	0x10
	.byte	0xaf
	.4byte	0x30c
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF133
	.byte	0x10
	.byte	0xb0
	.4byte	0x30c
	.byte	0x16
	.uleb128 0x15
	.4byte	.LASF134
	.byte	0x10
	.byte	0xb2
	.4byte	0x30c
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF135
	.byte	0x10
	.byte	0xb3
	.4byte	0x30c
	.byte	0x1a
	.uleb128 0x15
	.4byte	.LASF136
	.byte	0x10
	.byte	0xb4
	.4byte	0x30c
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF137
	.byte	0x10
	.byte	0xb5
	.4byte	0x30c
	.byte	0x1e
	.uleb128 0x15
	.4byte	.LASF138
	.byte	0x10
	.byte	0xb7
	.4byte	0x317
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF139
	.byte	0x10
	.byte	0xb8
	.4byte	0x317
	.byte	0x24
	.uleb128 0x15
	.4byte	.LASF140
	.byte	0x10
	.byte	0xb9
	.4byte	0x317
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF113
	.byte	0x10
	.byte	0xba
	.4byte	0x6fb
	.byte	0x2c
	.byte	0
	.uleb128 0x18
	.4byte	.LASF141
	.2byte	0x200
	.byte	0x10
	.byte	0xbe
	.4byte	0xaa6
	.uleb128 0x16
	.ascii	"tag\000"
	.byte	0x10
	.byte	0xc0
	.4byte	0x317
	.byte	0
	.uleb128 0x16
	.ascii	"ver\000"
	.byte	0x10
	.byte	0xc1
	.4byte	0x317
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF142
	.byte	0x10
	.byte	0xc3
	.4byte	0x317
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF143
	.byte	0x10
	.byte	0xc4
	.4byte	0x317
	.byte	0xc
	.uleb128 0x15
	.4byte	.LASF144
	.byte	0x10
	.byte	0xc5
	.4byte	0x317
	.byte	0x10
	.uleb128 0x15
	.4byte	.LASF145
	.byte	0x10
	.byte	0xc6
	.4byte	0x317
	.byte	0x14
	.uleb128 0x15
	.4byte	.LASF138
	.byte	0x10
	.byte	0xc8
	.4byte	0x317
	.byte	0x18
	.uleb128 0x15
	.4byte	.LASF146
	.byte	0x10
	.byte	0xc9
	.4byte	0x317
	.byte	0x1c
	.uleb128 0x15
	.4byte	.LASF147
	.byte	0x10
	.byte	0xca
	.4byte	0x317
	.byte	0x20
	.uleb128 0x15
	.4byte	.LASF148
	.byte	0x10
	.byte	0xcb
	.4byte	0x317
	.byte	0x24
	.uleb128 0x15
	.4byte	.LASF135
	.byte	0x10
	.byte	0xcd
	.4byte	0x317
	.byte	0x28
	.uleb128 0x15
	.4byte	.LASF149
	.byte	0x10
	.byte	0xce
	.4byte	0x317
	.byte	0x2c
	.uleb128 0x15
	.4byte	.LASF150
	.byte	0x10
	.byte	0xcf
	.4byte	0x317
	.byte	0x30
	.uleb128 0x15
	.4byte	.LASF151
	.byte	0x10
	.byte	0xd0
	.4byte	0x317
	.byte	0x34
	.uleb128 0x15
	.4byte	.LASF152
	.byte	0x10
	.byte	0xd2
	.4byte	0x317
	.byte	0x38
	.uleb128 0x15
	.4byte	.LASF153
	.byte	0x10
	.byte	0xd3
	.4byte	0x317
	.byte	0x3c
	.uleb128 0x15
	.4byte	.LASF154
	.byte	0x10
	.byte	0xd4
	.4byte	0x317
	.byte	0x40
	.uleb128 0x15
	.4byte	.LASF155
	.byte	0x10
	.byte	0xd5
	.4byte	0x317
	.byte	0x44
	.uleb128 0x15
	.4byte	.LASF156
	.byte	0x10
	.byte	0xd7
	.4byte	0x317
	.byte	0x48
	.uleb128 0x15
	.4byte	.LASF157
	.byte	0x10
	.byte	0xd8
	.4byte	0x317
	.byte	0x4c
	.uleb128 0x15
	.4byte	.LASF158
	.byte	0x10
	.byte	0xd9
	.4byte	0x317
	.byte	0x50
	.uleb128 0x15
	.4byte	.LASF159
	.byte	0x10
	.byte	0xda
	.4byte	0x317
	.byte	0x54
	.uleb128 0x15
	.4byte	.LASF160
	.byte	0x10
	.byte	0xdc
	.4byte	0x317
	.byte	0x58
	.uleb128 0x15
	.4byte	.LASF161
	.byte	0x10
	.byte	0xdd
	.4byte	0x317
	.byte	0x5c
	.uleb128 0x15
	.4byte	.LASF162
	.byte	0x10
	.byte	0xde
	.4byte	0x317
	.byte	0x60
	.uleb128 0x15
	.4byte	.LASF113
	.byte	0x10
	.byte	0xe0
	.4byte	0xaa6
	.byte	0x64
	.byte	0
	.uleb128 0xc
	.4byte	0x317
	.4byte	0xab6
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x66
	.byte	0
	.uleb128 0x14
	.4byte	.LASF163
	.byte	0x6
	.byte	0x10
	.byte	0xe4
	.4byte	0xae7
	.uleb128 0x15
	.4byte	.LASF164
	.byte	0x10
	.byte	0xe6
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF165
	.byte	0x10
	.byte	0xe7
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF166
	.byte	0x10
	.byte	0xe8
	.4byte	0x30c
	.byte	0x4
	.byte	0
	.uleb128 0x18
	.4byte	.LASF167
	.2byte	0x808
	.byte	0x10
	.byte	0xeb
	.4byte	0xb31
	.uleb128 0x16
	.ascii	"max\000"
	.byte	0x10
	.byte	0xed
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF168
	.byte	0x10
	.byte	0xee
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF169
	.byte	0x10
	.byte	0xef
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF115
	.byte	0x10
	.byte	0xf0
	.4byte	0x30c
	.byte	0x6
	.uleb128 0x16
	.ascii	"arr\000"
	.byte	0x10
	.byte	0xf1
	.4byte	0xb31
	.byte	0x8
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0xb42
	.uleb128 0x19
	.4byte	0x2e
	.2byte	0x3ff
	.byte	0
	.uleb128 0x14
	.4byte	.LASF170
	.byte	0x30
	.byte	0x10
	.byte	0xf5
	.4byte	0xbd4
	.uleb128 0x16
	.ascii	"id\000"
	.byte	0x10
	.byte	0xf7
	.4byte	0x30c
	.byte	0
	.uleb128 0x15
	.4byte	.LASF171
	.byte	0x10
	.byte	0xf8
	.4byte	0x30c
	.byte	0x2
	.uleb128 0x15
	.4byte	.LASF172
	.byte	0x10
	.byte	0xf9
	.4byte	0x30c
	.byte	0x4
	.uleb128 0x15
	.4byte	.LASF173
	.byte	0x10
	.byte	0xfa
	.4byte	0x301
	.byte	0x6
	.uleb128 0x15
	.4byte	.LASF174
	.byte	0x10
	.byte	0xfb
	.4byte	0x301
	.byte	0x7
	.uleb128 0x15
	.4byte	.LASF175
	.byte	0x10
	.byte	0xfc
	.4byte	0x301
	.byte	0x8
	.uleb128 0x15
	.4byte	.LASF176
	.byte	0x10
	.byte	0xfd
	.4byte	0x301
	.byte	0x9
	.uleb128 0x15
	.4byte	.LASF177
	.byte	0x10
	.byte	0xfe
	.4byte	0x301
	.byte	0xa
	.uleb128 0x15
	.4byte	.LASF178
	.byte	0x10
	.byte	0xff
	.4byte	0x301
	.byte	0xb
	.uleb128 0x1a
	.4byte	.LASF88
	.byte	0x10
	.2byte	0x100
	.4byte	0x317
	.byte	0xc
	.uleb128 0x1a
	.4byte	.LASF179
	.byte	0x10
	.2byte	0x101
	.4byte	0xbd4
	.byte	0x10
	.byte	0
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0xbe4
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xf
	.byte	0
	.uleb128 0x1b
	.4byte	.LASF180
	.byte	0xc
	.byte	0x10
	.2byte	0x105
	.4byte	0xc19
	.uleb128 0x1a
	.4byte	.LASF181
	.byte	0x10
	.2byte	0x107
	.4byte	0x317
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF182
	.byte	0x10
	.2byte	0x108
	.4byte	0x317
	.byte	0x4
	.uleb128 0x1c
	.ascii	"lpa\000"
	.byte	0x10
	.2byte	0x109
	.4byte	0x317
	.byte	0x8
	.byte	0
	.uleb128 0x1b
	.4byte	.LASF183
	.byte	0xc
	.byte	0x10
	.2byte	0x10c
	.4byte	0xc4e
	.uleb128 0x1a
	.4byte	.LASF64
	.byte	0x10
	.2byte	0x10e
	.4byte	0x544
	.byte	0
	.uleb128 0x1a
	.4byte	.LASF65
	.byte	0x10
	.2byte	0x10f
	.4byte	0x544
	.byte	0x4
	.uleb128 0x1a
	.4byte	.LASF184
	.byte	0x10
	.2byte	0x110
	.4byte	0x317
	.byte	0x8
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF185
	.byte	0x11
	.byte	0x12
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_type
	.uleb128 0x1d
	.4byte	.LASF186
	.byte	0x11
	.byte	0x13
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_die_num
	.uleb128 0x1d
	.4byte	.LASF187
	.byte	0x11
	.byte	0x14
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_planes_per_die
	.uleb128 0x1d
	.4byte	.LASF188
	.byte	0x11
	.byte	0x15
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blks_per_die
	.uleb128 0x1d
	.4byte	.LASF189
	.byte	0x11
	.byte	0x16
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blks_per_die_shift
	.uleb128 0x1d
	.4byte	.LASF190
	.byte	0x11
	.byte	0x17
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_planes_num
	.uleb128 0x1d
	.4byte	.LASF191
	.byte	0x11
	.byte	0x18
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_blk_pre_plane
	.uleb128 0x1d
	.4byte	.LASF192
	.byte	0x11
	.byte	0x19
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_ext_blk_pre_plane
	.uleb128 0x1d
	.4byte	.LASF193
	.byte	0x11
	.byte	0x1a
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_bbm_buf_size
	.uleb128 0x1d
	.4byte	.LASF194
	.byte	0x11
	.byte	0x1c
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_blk
	.uleb128 0x1d
	.4byte	.LASF195
	.byte	0x11
	.byte	0x1d
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_slc_blk
	.uleb128 0x1d
	.4byte	.LASF196
	.byte	0x11
	.byte	0x1e
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_page_pre_super_blk
	.uleb128 0x1d
	.4byte	.LASF197
	.byte	0x11
	.byte	0x1f
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sec_pre_page
	.uleb128 0x1d
	.4byte	.LASF198
	.byte	0x11
	.byte	0x20
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sec_pre_page_shift
	.uleb128 0x1d
	.4byte	.LASF199
	.byte	0x11
	.byte	0x24
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_byte_pre_page
	.uleb128 0x1d
	.4byte	.LASF200
	.byte	0x11
	.byte	0x25
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_byte_pre_oob
	.uleb128 0x1d
	.4byte	.LASF201
	.byte	0x11
	.byte	0x26
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_reserved_blks
	.uleb128 0x1d
	.4byte	.LASF202
	.byte	0x11
	.byte	0x27
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_totle_phy_blks
	.uleb128 0x1d
	.4byte	.LASF203
	.byte	0x11
	.byte	0x29
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_map_region_num
	.uleb128 0x1d
	.4byte	.LASF204
	.byte	0x11
	.byte	0x2a
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_l2pmap_ram_region_num
	.uleb128 0x1d
	.4byte	.LASF205
	.byte	0x11
	.byte	0x2c
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_vendor_region_num
	.uleb128 0x1d
	.4byte	.LASF206
	.byte	0x11
	.byte	0x2e
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_map_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF207
	.byte	0x11
	.byte	0x2f
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_map_blks
	.uleb128 0x1d
	.4byte	.LASF208
	.byte	0x11
	.byte	0x30
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_vendor_blks
	.uleb128 0x1d
	.4byte	.LASF209
	.byte	0x11
	.byte	0x31
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_vendor_part_size
	.uleb128 0x1d
	.4byte	.LASF210
	.byte	0x11
	.byte	0x32
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_sys_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF211
	.byte	0x11
	.byte	0x33
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_init_sys_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF212
	.byte	0x11
	.byte	0x34
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_sys_blks
	.uleb128 0x1d
	.4byte	.LASF213
	.byte	0x11
	.byte	0x35
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_data_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF214
	.byte	0x11
	.byte	0x36
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_data_op_blks_per_plane
	.uleb128 0x1d
	.4byte	.LASF215
	.byte	0x11
	.byte	0x37
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_ftl_nand_max_data_blks
	.uleb128 0x1d
	.4byte	.LASF216
	.byte	0x11
	.byte	0x38
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_gc_temp_power_lost_recovery_flag
	.uleb128 0x1d
	.4byte	.LASF217
	.byte	0x11
	.byte	0x3a
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_GlobalSysVersion
	.uleb128 0x1d
	.4byte	.LASF218
	.byte	0x11
	.byte	0x3b
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_GlobalDataVersion
	.uleb128 0x1d
	.4byte	.LASF219
	.byte	0x11
	.byte	0x3c
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLbaSector
	.uleb128 0x1d
	.4byte	.LASF220
	.byte	0x11
	.byte	0x3d
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLpn
	.uleb128 0x1d
	.4byte	.LASF221
	.byte	0x11
	.byte	0x3e
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_VaildLpn
	.uleb128 0x1d
	.4byte	.LASF222
	.byte	0x11
	.byte	0x3f
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_MaxLbn
	.uleb128 0x1d
	.4byte	.LASF223
	.byte	0x11
	.byte	0x41
	.4byte	0x4c4
	.uleb128 0x5
	.byte	0x3
	.4byte	gBbtInfo
	.uleb128 0x1d
	.4byte	.LASF224
	.byte	0x11
	.byte	0x42
	.4byte	0x54a
	.uleb128 0x5
	.byte	0x3
	.4byte	gSysInfo
	.uleb128 0x1d
	.4byte	.LASF225
	.byte	0x11
	.byte	0x43
	.4byte	0xae7
	.uleb128 0x5
	.byte	0x3
	.4byte	gSysFreeQueue
	.uleb128 0x1d
	.4byte	.LASF226
	.byte	0x11
	.byte	0x44
	.4byte	0x592
	.uleb128 0x5
	.byte	0x3
	.4byte	gL2pMapInfo
	.uleb128 0x1d
	.4byte	.LASF227
	.byte	0x11
	.byte	0x45
	.4byte	0x592
	.uleb128 0x5
	.byte	0x3
	.4byte	gVendorBlkInfo
	.uleb128 0x1d
	.4byte	.LASF228
	.byte	0x11
	.byte	0x46
	.4byte	0x338
	.uleb128 0x5
	.byte	0x3
	.4byte	req_sys
	.uleb128 0x1d
	.4byte	.LASF229
	.byte	0x11
	.byte	0x47
	.4byte	0xf4b
	.uleb128 0x5
	.byte	0x3
	.4byte	req_read
	.uleb128 0x8
	.byte	0x4
	.4byte	0x338
	.uleb128 0x1d
	.4byte	.LASF230
	.byte	0x11
	.byte	0x48
	.4byte	0xf4b
	.uleb128 0x5
	.byte	0x3
	.4byte	req_prgm
	.uleb128 0x1d
	.4byte	.LASF231
	.byte	0x11
	.byte	0x49
	.4byte	0xf4b
	.uleb128 0x5
	.byte	0x3
	.4byte	req_erase
	.uleb128 0x1d
	.4byte	.LASF232
	.byte	0x11
	.byte	0x4a
	.4byte	0xf4b
	.uleb128 0x5
	.byte	0x3
	.4byte	req_gc
	.uleb128 0x1d
	.4byte	.LASF233
	.byte	0x11
	.byte	0x4b
	.4byte	0xf4b
	.uleb128 0x5
	.byte	0x3
	.4byte	req_gc_dst
	.uleb128 0x1d
	.4byte	.LASF234
	.byte	0x11
	.byte	0x4d
	.4byte	0xf4b
	.uleb128 0x5
	.byte	0x3
	.4byte	g_req_cache
	.uleb128 0xc
	.4byte	0x301
	.4byte	0xfb6
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1f
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF235
	.byte	0x11
	.byte	0x4f
	.4byte	0xfa6
	.uleb128 0x5
	.byte	0x3
	.4byte	p_plane_order_table
	.uleb128 0x1d
	.4byte	.LASF236
	.byte	0x11
	.byte	0x51
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_data_buf
	.uleb128 0x1d
	.4byte	.LASF237
	.byte	0x11
	.byte	0x52
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_data_buf_1
	.uleb128 0x1d
	.4byte	.LASF238
	.byte	0x11
	.byte	0x53
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_data_buf
	.uleb128 0x1d
	.4byte	.LASF239
	.byte	0x11
	.byte	0x54
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_sys_spare_buf
	.uleb128 0x1d
	.4byte	.LASF240
	.byte	0x11
	.byte	0x55
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_data_buf_0
	.uleb128 0x1d
	.4byte	.LASF241
	.byte	0x11
	.byte	0x56
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_data_buf_1
	.uleb128 0x1d
	.4byte	.LASF242
	.byte	0x11
	.byte	0x57
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_io_spare_buf
	.uleb128 0x1d
	.4byte	.LASF243
	.byte	0x11
	.byte	0x58
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_spare_buf
	.uleb128 0x1d
	.4byte	.LASF244
	.byte	0x11
	.byte	0x59
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_data_buf
	.uleb128 0x1d
	.4byte	.LASF245
	.byte	0x11
	.byte	0x5a
	.4byte	0x1071
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_gc_page_buf_info
	.uleb128 0x8
	.byte	0x4
	.4byte	0xc19
	.uleb128 0x1d
	.4byte	.LASF246
	.byte	0x11
	.byte	0x5b
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	c_gc_page_buf_num
	.uleb128 0x1d
	.4byte	.LASF247
	.byte	0x11
	.byte	0x5c
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_num_req
	.uleb128 0x1d
	.4byte	.LASF248
	.byte	0x11
	.byte	0x5d
	.4byte	0x10aa
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_ect_tbl_info
	.uleb128 0x8
	.byte	0x4
	.4byte	0x67e
	.uleb128 0x1d
	.4byte	.LASF249
	.byte	0x11
	.byte	0x5e
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_info_size
	.uleb128 0x1d
	.4byte	.LASF250
	.byte	0x11
	.byte	0x5f
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_erase_count_table
	.uleb128 0x1d
	.4byte	.LASF251
	.byte	0x11
	.byte	0x61
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_swl_mul_table
	.uleb128 0x1d
	.4byte	.LASF252
	.byte	0x11
	.byte	0x62
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_swl_count
	.uleb128 0x1d
	.4byte	.LASF253
	.byte	0x11
	.byte	0x63
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_valid_page_count_table
	.uleb128 0x1d
	.4byte	.LASF254
	.byte	0x11
	.byte	0x64
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_valid_page_count_check_table
	.uleb128 0x1d
	.4byte	.LASF255
	.byte	0x11
	.byte	0x65
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_blk_mode_table
	.uleb128 0x1d
	.4byte	.LASF256
	.byte	0x11
	.byte	0x67
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_table
	.uleb128 0x1d
	.4byte	.LASF257
	.byte	0x11
	.byte	0x68
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_valid_page_count
	.uleb128 0x1d
	.4byte	.LASF258
	.byte	0x11
	.byte	0x69
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_block_ver_table
	.uleb128 0x1d
	.4byte	.LASF259
	.byte	0x11
	.byte	0x6a
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_map_region_ppn_table
	.uleb128 0x1d
	.4byte	.LASF260
	.byte	0x11
	.byte	0x6b
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_map_block
	.uleb128 0x1d
	.4byte	.LASF261
	.byte	0x11
	.byte	0x6d
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_table
	.uleb128 0x1d
	.4byte	.LASF262
	.byte	0x11
	.byte	0x6e
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_valid_page_count
	.uleb128 0x1d
	.4byte	.LASF263
	.byte	0x11
	.byte	0x6f
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_block_ver_table
	.uleb128 0x1d
	.4byte	.LASF264
	.byte	0x11
	.byte	0x70
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_vendor_region_ppn_table
	.uleb128 0x1d
	.4byte	.LASF265
	.byte	0x11
	.byte	0x71
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_vendor_block
	.uleb128 0x1d
	.4byte	.LASF266
	.byte	0x11
	.byte	0x73
	.4byte	0x11e2
	.uleb128 0x5
	.byte	0x3
	.4byte	p_l2p_ram_map
	.uleb128 0x8
	.byte	0x4
	.4byte	0x641
	.uleb128 0x1d
	.4byte	.LASF267
	.byte	0x11
	.byte	0x74
	.4byte	0x544
	.uleb128 0x5
	.byte	0x3
	.4byte	p_l2p_map_buf
	.uleb128 0x1d
	.4byte	.LASF268
	.byte	0x11
	.byte	0x75
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_l2p_last_update_region_id
	.uleb128 0x1d
	.4byte	.LASF269
	.byte	0x11
	.byte	0x7c
	.4byte	0x121b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_table
	.uleb128 0x8
	.byte	0x4
	.4byte	0xab6
	.uleb128 0x1d
	.4byte	.LASF270
	.byte	0x11
	.byte	0x7d
	.4byte	0x121b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_free_data_block_list_head
	.uleb128 0x1d
	.4byte	.LASF271
	.byte	0x11
	.byte	0x7f
	.4byte	0x121b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_head
	.uleb128 0x1d
	.4byte	.LASF272
	.byte	0x11
	.byte	0x80
	.4byte	0x121b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_data_block_list_tail
	.uleb128 0x1d
	.4byte	.LASF273
	.byte	0x11
	.byte	0x81
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_num_free_superblocks
	.uleb128 0x1d
	.4byte	.LASF274
	.byte	0x11
	.byte	0x82
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_num_data_superblocks
	.uleb128 0x1d
	.4byte	.LASF275
	.byte	0x11
	.byte	0x83
	.4byte	0xb42
	.uleb128 0x5
	.byte	0x3
	.4byte	g_active_superblock
	.uleb128 0x1d
	.4byte	.LASF276
	.byte	0x11
	.byte	0x84
	.4byte	0xb42
	.uleb128 0x5
	.byte	0x3
	.4byte	g_buffer_superblock
	.uleb128 0x1d
	.4byte	.LASF277
	.byte	0x11
	.byte	0x85
	.4byte	0xb42
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_temp_superblock
	.uleb128 0x1d
	.4byte	.LASF278
	.byte	0x11
	.byte	0x86
	.4byte	0xb42
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_superblock
	.uleb128 0x1d
	.4byte	.LASF279
	.byte	0x11
	.byte	0x87
	.4byte	0x12cb
	.uleb128 0x5
	.byte	0x3
	.4byte	gp_last_act_superblock
	.uleb128 0x8
	.byte	0x4
	.4byte	0xb42
	.uleb128 0x1d
	.4byte	.LASF280
	.byte	0x11
	.byte	0x88
	.4byte	0x863
	.uleb128 0x5
	.byte	0x3
	.4byte	g_sys_save_data
	.uleb128 0x1d
	.4byte	.LASF281
	.byte	0x11
	.byte	0x89
	.4byte	0x960
	.uleb128 0x5
	.byte	0x3
	.4byte	g_sys_ext_data
	.uleb128 0x1d
	.4byte	.LASF282
	.byte	0x11
	.byte	0x8b
	.4byte	0x1304
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_page_info
	.uleb128 0x8
	.byte	0x4
	.4byte	0xbe4
	.uleb128 0x1d
	.4byte	.LASF283
	.byte	0x11
	.byte	0x8c
	.4byte	0x63b
	.uleb128 0x5
	.byte	0x3
	.4byte	p_gc_blk_tbl
	.uleb128 0x1d
	.4byte	.LASF284
	.byte	0x11
	.byte	0x8d
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_blk_num
	.uleb128 0x1d
	.4byte	.LASF285
	.byte	0x11
	.byte	0x8e
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_page_offset
	.uleb128 0x1d
	.4byte	.LASF286
	.byte	0x11
	.byte	0x8f
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_cur_blk_valid_pages
	.uleb128 0x1d
	.4byte	.LASF287
	.byte	0x11
	.byte	0x90
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_cur_blk_max_valid_pages
	.uleb128 0x1d
	.4byte	.LASF288
	.byte	0x11
	.byte	0x91
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_next_blk
	.uleb128 0x1d
	.4byte	.LASF289
	.byte	0x11
	.byte	0x92
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_next_blk_1
	.uleb128 0x1d
	.4byte	.LASF290
	.byte	0x11
	.byte	0x93
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_temp_num
	.uleb128 0x1d
	.4byte	.LASF291
	.byte	0x11
	.byte	0x94
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_gc_index
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x13b3
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x10
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF292
	.byte	0x11
	.byte	0x95
	.4byte	0x13a3
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_bad_block_temp_tbl
	.uleb128 0x1d
	.4byte	.LASF293
	.byte	0x11
	.byte	0x97
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_free_blk_threshold
	.uleb128 0x1d
	.4byte	.LASF294
	.byte	0x11
	.byte	0x98
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_merge_free_blk_threshold
	.uleb128 0x1d
	.4byte	.LASF295
	.byte	0x11
	.byte	0x99
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_blk_index
	.uleb128 0x1d
	.4byte	.LASF296
	.byte	0x11
	.byte	0x9b
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_gc_page_count
	.uleb128 0x1d
	.4byte	.LASF297
	.byte	0x11
	.byte	0x9c
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_write_page_count
	.uleb128 0x1d
	.4byte	.LASF298
	.byte	0x11
	.byte	0x9d
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_write_sector
	.uleb128 0x1d
	.4byte	.LASF299
	.byte	0x11
	.byte	0x9e
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_read_sector
	.uleb128 0x1d
	.4byte	.LASF300
	.byte	0x11
	.byte	0xa0
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_discard_page_count
	.uleb128 0x1d
	.4byte	.LASF301
	.byte	0x11
	.byte	0xa1
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_read_page_count
	.uleb128 0x1d
	.4byte	.LASF302
	.byte	0x11
	.byte	0xa2
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_tmp_data_superblock_id
	.uleb128 0x1d
	.4byte	.LASF303
	.byte	0x11
	.byte	0xa3
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_cache_write_count
	.uleb128 0x1d
	.4byte	.LASF304
	.byte	0x11
	.byte	0xa4
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_l2p_write_count
	.uleb128 0x1d
	.4byte	.LASF305
	.byte	0x11
	.byte	0xa6
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_mlc_erase_count
	.uleb128 0x1d
	.4byte	.LASF306
	.byte	0x11
	.byte	0xa7
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_avg_erase_count
	.uleb128 0x1d
	.4byte	.LASF307
	.byte	0x11
	.byte	0xa8
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_min_erase_count
	.uleb128 0x1d
	.4byte	.LASF308
	.byte	0x11
	.byte	0xa9
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_slc_erase_count
	.uleb128 0x1d
	.4byte	.LASF309
	.byte	0x11
	.byte	0xaa
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_totle_sys_slc_erase_count
	.uleb128 0x1d
	.4byte	.LASF310
	.byte	0x11
	.byte	0xab
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_max_erase_count
	.uleb128 0x1d
	.4byte	.LASF311
	.byte	0x11
	.byte	0xac
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_in_gc_progress
	.uleb128 0x1d
	.4byte	.LASF312
	.byte	0x11
	.byte	0xad
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_in_swl_replace
	.uleb128 0x1d
	.4byte	.LASF313
	.byte	0x11
	.byte	0xae
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ftl_nand_free_count
	.uleb128 0x1d
	.4byte	.LASF314
	.byte	0x11
	.byte	0xaf
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_head_data_block
	.uleb128 0x1d
	.4byte	.LASF315
	.byte	0x11
	.byte	0xb0
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_head_data_block_count
	.uleb128 0x1d
	.4byte	.LASF316
	.byte	0x11
	.byte	0xb1
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_gc_skip_write_count
	.uleb128 0x1d
	.4byte	.LASF317
	.byte	0x11
	.byte	0xb2
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_cur_erase_blk
	.uleb128 0x1d
	.4byte	.LASF318
	.byte	0x11
	.byte	0xb4
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_page_num
	.uleb128 0x1d
	.4byte	.LASF319
	.byte	0x11
	.byte	0xb5
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_page_min_ver
	.uleb128 0xc
	.4byte	0x317
	.4byte	0x159f
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1f
	.byte	0
	.uleb128 0x1d
	.4byte	.LASF320
	.byte	0x11
	.byte	0xb6
	.4byte	0x158f
	.uleb128 0x5
	.byte	0x3
	.4byte	g_recovery_ppa_tbl
	.uleb128 0x1d
	.4byte	.LASF321
	.byte	0x11
	.byte	0xb7
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	c_mlc_erase_count_value
	.uleb128 0x1d
	.4byte	.LASF322
	.byte	0x11
	.byte	0xb8
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_power_lost_recovery_flag
	.uleb128 0x9
	.4byte	.LASF323
	.byte	0xe
	.byte	0x14
	.4byte	0x387
	.uleb128 0x9
	.4byte	.LASF324
	.byte	0xe
	.byte	0x15
	.4byte	0x387
	.uleb128 0x9
	.4byte	.LASF325
	.byte	0xe
	.byte	0x16
	.4byte	0x429
	.uleb128 0xc
	.4byte	0x99
	.4byte	0x1604
	.uleb128 0x19
	.4byte	0x2e
	.2byte	0x3ff
	.byte	0
	.uleb128 0x9
	.4byte	.LASF326
	.byte	0xe
	.byte	0x17
	.4byte	0x15f3
	.uleb128 0x1d
	.4byte	.LASF327
	.byte	0x3
	.byte	0x5
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	DeviceCapacity
	.uleb128 0x1d
	.4byte	.LASF328
	.byte	0x3
	.byte	0x6
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	gFtlInitStatus
	.uleb128 0x9
	.4byte	.LASF329
	.byte	0x3
	.byte	0x6d
	.4byte	0x317
	.uleb128 0x9
	.4byte	.LASF330
	.byte	0x3
	.byte	0x6e
	.4byte	0x317
	.uleb128 0x9
	.4byte	.LASF331
	.byte	0x3
	.byte	0x6f
	.4byte	0x317
	.uleb128 0x9
	.4byte	.LASF332
	.byte	0x3
	.byte	0x70
	.4byte	0x317
	.uleb128 0x1e
	.4byte	.LASF333
	.byte	0x3
	.2byte	0x3bd
	.4byte	0x64
	.uleb128 0x5
	.byte	0x3
	.4byte	power_up_flag
	.uleb128 0x1f
	.4byte	.LASF337
	.byte	0x3
	.2byte	0x480
	.4byte	0x317
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_discard_updated
	.uleb128 0x1e
	.4byte	.LASF334
	.byte	0x2
	.2byte	0x35b
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	g_ect_tbl_power_up_flush
	.uleb128 0x1e
	.4byte	.LASF335
	.byte	0x2
	.2byte	0x370
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	FtlUpdateVaildLpnCount
	.uleb128 0xc
	.4byte	0x30c
	.4byte	0x16b6
	.uleb128 0x19
	.4byte	0x2e
	.2byte	0x1fff
	.byte	0
	.uleb128 0x1e
	.4byte	.LASF336
	.byte	0x2
	.2byte	0x8ad
	.4byte	0x16a5
	.uleb128 0x5
	.byte	0x3
	.4byte	check_vpc_table
	.uleb128 0x20
	.4byte	.LASF338
	.byte	0x5
	.byte	0x6d
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_gc_temp_block_bops_scan_page_addr
	.uleb128 0x1e
	.4byte	.LASF339
	.byte	0x5
	.2byte	0x1d4
	.4byte	0x30c
	.uleb128 0x5
	.byte	0x3
	.4byte	gc_ink_free_return_value
	.uleb128 0x20
	.4byte	.LASF340
	.byte	0x1
	.byte	0xe
	.4byte	0x15f3
	.uleb128 0x5
	.byte	0x3
	.4byte	check_buf
	.uleb128 0x21
	.4byte	0x1604
	.byte	0x1
	.byte	0xf
	.uleb128 0x5
	.byte	0x3
	.4byte	ftl_temp_buf
	.uleb128 0xc
	.4byte	0x99
	.4byte	0x1719
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x7f
	.byte	0
	.uleb128 0x20
	.4byte	.LASF341
	.byte	0x1
	.byte	0x10
	.4byte	0x1709
	.uleb128 0x5
	.byte	0x3
	.4byte	check_spare_buf
	.uleb128 0x22
	.4byte	.LASF344
	.byte	0x1
	.byte	0xd2
	.4byte	0x64
	.4byte	.LFB269
	.4byte	.LFE269-.LFB269
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x177a
	.uleb128 0x23
	.4byte	.LASF342
	.byte	0x1
	.byte	0xd2
	.4byte	0x25
	.4byte	.LLST290
	.uleb128 0x23
	.4byte	.LASF343
	.byte	0x1
	.byte	0xd2
	.4byte	0x25
	.4byte	.LLST291
	.uleb128 0x23
	.4byte	.LASF115
	.byte	0x1
	.byte	0xd2
	.4byte	0x41
	.4byte	.LLST292
	.uleb128 0x24
	.4byte	.LVL1646
	.4byte	0x6708
	.byte	0
	.uleb128 0x22
	.4byte	.LASF345
	.byte	0x1
	.byte	0xcd
	.4byte	0x25
	.4byte	.LFB268
	.4byte	.LFE268-.LFB268
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x17ca
	.uleb128 0x23
	.4byte	.LASF346
	.byte	0x1
	.byte	0xcd
	.4byte	0x25
	.4byte	.LLST170
	.uleb128 0x23
	.4byte	.LASF347
	.byte	0x1
	.byte	0xcd
	.4byte	0x17ca
	.4byte	.LLST171
	.uleb128 0x23
	.4byte	.LASF348
	.byte	0x1
	.byte	0xcd
	.4byte	0x41
	.4byte	.LLST172
	.uleb128 0x24
	.4byte	.LVL835
	.4byte	0x6713
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x17d0
	.uleb128 0x25
	.uleb128 0x26
	.4byte	.LASF351
	.byte	0x1
	.byte	0xc8
	.4byte	0x25
	.byte	0x1
	.4byte	0x1815
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
	.4byte	.LASF349
	.byte	0x1
	.byte	0xb6
	.4byte	0x8e
	.4byte	.LFB266
	.4byte	.LFE266-.LFB266
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x190b
	.uleb128 0x23
	.4byte	.LASF350
	.byte	0x1
	.byte	0xb6
	.4byte	0x47a
	.4byte	.LLST104
	.uleb128 0x2b
	.ascii	"die\000"
	.byte	0x1
	.byte	0xb6
	.4byte	0x99
	.4byte	.LLST105
	.uleb128 0x2c
	.4byte	.LASF115
	.byte	0x1
	.byte	0xb8
	.4byte	0x83
	.4byte	.LLST106
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x1
	.byte	0xb8
	.4byte	0x83
	.uleb128 0x2d
	.4byte	0x17d1
	.4byte	.LBB337
	.4byte	.Ldebug_ranges0+0xd8
	.byte	0x1
	.byte	0xba
	.4byte	0x18b5
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xd8
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xd8
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.uleb128 0x24
	.4byte	.LVL468
	.4byte	0x671c
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x31
	.4byte	0x17d1
	.4byte	.LBB346
	.4byte	.LBE346-.LBB346
	.byte	0x1
	.byte	0xbd
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB347
	.4byte	.LBE347-.LBB347
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB348
	.4byte	.LBE348-.LBB348
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.uleb128 0x24
	.4byte	.LVL472
	.4byte	0x671c
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x26
	.4byte	.LASF352
	.byte	0x1
	.byte	0x9b
	.4byte	0x322
	.byte	0x1
	.4byte	0x194f
	.uleb128 0x33
	.4byte	.LASF353
	.byte	0x1
	.byte	0x9b
	.4byte	0x30c
	.uleb128 0x29
	.ascii	"sts\000"
	.byte	0x1
	.byte	0x9d
	.4byte	0x322
	.uleb128 0x34
	.4byte	.LASF354
	.byte	0x1
	.byte	0x9e
	.4byte	0x194f
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x9f
	.4byte	0x338
	.uleb128 0x35
	.ascii	"out\000"
	.byte	0x1
	.byte	0xb1
	.byte	0
	.uleb128 0xc
	.4byte	0x317
	.4byte	0x195f
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xf
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF355
	.byte	0x1
	.byte	0x85
	.4byte	0x8e
	.4byte	.LFB264
	.4byte	.LFE264-.LFB264
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x19f5
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x85
	.4byte	0x25
	.4byte	.LLST91
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x85
	.4byte	0x99
	.4byte	.LLST92
	.uleb128 0x23
	.4byte	.LASF358
	.byte	0x1
	.byte	0x85
	.4byte	0x99
	.4byte	.LLST93
	.uleb128 0x36
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
	.uleb128 0x34
	.4byte	.LASF62
	.byte	0x1
	.byte	0x87
	.4byte	0x99
	.uleb128 0x20
	.4byte	.LASF359
	.byte	0x1
	.byte	0x87
	.4byte	0x99
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x88
	.4byte	0xf4b
	.uleb128 0x20
	.4byte	.LASF360
	.byte	0x1
	.byte	0x89
	.4byte	0x99
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x24
	.4byte	.LVL403
	.4byte	0x1bb3
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF361
	.byte	0x1
	.byte	0x49
	.4byte	0x8e
	.4byte	.LFB263
	.4byte	.LFE263-.LFB263
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1af0
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x49
	.4byte	0x25
	.4byte	.LLST86
	.uleb128 0x23
	.4byte	.LASF358
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST87
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST88
	.uleb128 0x23
	.4byte	.LASF362
	.byte	0x1
	.byte	0x49
	.4byte	0x99
	.4byte	.LLST89
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
	.uleb128 0x34
	.4byte	.LASF62
	.byte	0x1
	.byte	0x4b
	.4byte	0x99
	.uleb128 0x20
	.4byte	.LASF359
	.byte	0x1
	.byte	0x4b
	.4byte	0x99
	.uleb128 0x3
	.byte	0x91
	.sleb128 -72
	.uleb128 0x2c
	.4byte	.LASF360
	.byte	0x1
	.byte	0x4c
	.4byte	0x99
	.4byte	.LLST90
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x4d
	.4byte	0xf4b
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x1b00
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7625
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x88
	.4byte	0x1add
	.uleb128 0x20
	.4byte	.LASF364
	.byte	0x1
	.byte	0x68
	.4byte	0x338
	.uleb128 0x3
	.byte	0x91
	.sleb128 -68
	.uleb128 0x24
	.4byte	.LVL388
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL389
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL390
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL391
	.4byte	0x6727
	.byte	0
	.uleb128 0x24
	.4byte	.LVL377
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL379
	.4byte	0x1bb3
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x1b00
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xe
	.byte	0
	.uleb128 0x4
	.4byte	0x1af0
	.uleb128 0x2a
	.4byte	.LASF365
	.byte	0x1
	.byte	0x2b
	.4byte	0x8e
	.4byte	.LFB262
	.4byte	.LFE262-.LFB262
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1bb3
	.uleb128 0x23
	.4byte	.LASF356
	.byte	0x1
	.byte	0x2b
	.4byte	0x25
	.4byte	.LLST79
	.uleb128 0x23
	.4byte	.LASF358
	.byte	0x1
	.byte	0x2b
	.4byte	0x99
	.4byte	.LLST80
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x1
	.byte	0x2b
	.4byte	0x99
	.4byte	.LLST81
	.uleb128 0x36
	.ascii	"i\000"
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"cs\000"
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x34
	.4byte	.LASF62
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x20
	.4byte	.LASF359
	.byte	0x1
	.byte	0x2d
	.4byte	0x99
	.uleb128 0x2
	.byte	0x91
	.sleb128 -52
	.uleb128 0x20
	.4byte	.LASF360
	.byte	0x1
	.byte	0x2e
	.4byte	0x99
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x1
	.byte	0x2f
	.4byte	0xf4b
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x1b00
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7608
	.uleb128 0x24
	.4byte	.LVL348
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL351
	.4byte	0x1bb3
	.byte	0
	.uleb128 0x39
	.4byte	.LASF669
	.byte	0x1
	.byte	0x11
	.4byte	0x99
	.4byte	.LFB261
	.4byte	.LFE261-.LFB261
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c5b
	.uleb128 0x2b
	.ascii	"req\000"
	.byte	0x1
	.byte	0x11
	.4byte	0xf4b
	.4byte	.LLST0
	.uleb128 0x23
	.4byte	.LASF359
	.byte	0x1
	.byte	0x11
	.4byte	0x381
	.4byte	.LLST1
	.uleb128 0x23
	.4byte	.LASF366
	.byte	0x1
	.byte	0x11
	.4byte	0x381
	.4byte	.LLST2
	.uleb128 0x34
	.4byte	.LASF367
	.byte	0x1
	.byte	0x13
	.4byte	0x83
	.uleb128 0x34
	.4byte	.LASF368
	.byte	0x1
	.byte	0x13
	.4byte	0x83
	.uleb128 0x2c
	.4byte	.LASF72
	.byte	0x1
	.byte	0x14
	.4byte	0x83
	.4byte	.LLST3
	.uleb128 0x2c
	.4byte	.LASF73
	.byte	0x1
	.byte	0x14
	.4byte	0x83
	.4byte	.LLST4
	.uleb128 0x2c
	.4byte	.LASF369
	.byte	0x1
	.byte	0x15
	.4byte	0x99
	.4byte	.LLST5
	.uleb128 0x2c
	.4byte	.LASF63
	.byte	0x1
	.byte	0x16
	.4byte	0x99
	.4byte	.LLST6
	.uleb128 0x2c
	.4byte	.LASF370
	.byte	0x1
	.byte	0x17
	.4byte	0x99
	.4byte	.LLST7
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF371
	.byte	0x5
	.2byte	0x2f8
	.4byte	0x99
	.4byte	.LFB260
	.4byte	.LFE260-.LFB260
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1c7f
	.uleb128 0x24
	.4byte	.LVL1386
	.4byte	0x1c7f
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF372
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x64
	.byte	0x1
	.4byte	0x1d4c
	.uleb128 0x3c
	.4byte	.LASF373
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x317
	.uleb128 0x3c
	.4byte	.LASF374
	.byte	0x5
	.2byte	0x1d5
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF375
	.byte	0x5
	.2byte	0x1d7
	.4byte	0x30c
	.uleb128 0x3e
	.ascii	"lpa\000"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x317
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x5
	.2byte	0x1d8
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF376
	.byte	0x5
	.2byte	0x1d9
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF86
	.byte	0x5
	.2byte	0x1da
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x5
	.2byte	0x1da
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x5
	.2byte	0x1da
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x5
	.2byte	0x1da
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF380
	.byte	0x5
	.2byte	0x1db
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF381
	.byte	0x5
	.2byte	0x1dc
	.4byte	0x1d4c
	.uleb128 0x3f
	.4byte	.LASF382
	.byte	0x5
	.2byte	0x276
	.uleb128 0x3f
	.4byte	.LASF383
	.byte	0x5
	.2byte	0x279
	.uleb128 0x40
	.4byte	.LASF363
	.4byte	0x1d62
	.4byte	.LASF372
	.uleb128 0x28
	.uleb128 0x3d
	.4byte	.LASF384
	.byte	0x5
	.2byte	0x1f3
	.4byte	0x30c
	.byte	0
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x77b
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x1d62
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x16
	.byte	0
	.uleb128 0x4
	.4byte	0x1d52
	.uleb128 0x3b
	.4byte	.LASF385
	.byte	0x5
	.2byte	0x1a2
	.4byte	0x64
	.byte	0x1
	.4byte	0x1dc7
	.uleb128 0x3e
	.ascii	"ret\000"
	.byte	0x5
	.2byte	0x1a4
	.4byte	0x64
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x1a5
	.4byte	0x64
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x5
	.2byte	0x1a6
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF354
	.byte	0x5
	.2byte	0x1a7
	.4byte	0x194f
	.uleb128 0x41
	.4byte	0x1db8
	.uleb128 0x3d
	.4byte	.LASF386
	.byte	0x5
	.2byte	0x1b1
	.4byte	0x338
	.byte	0
	.uleb128 0x28
	.uleb128 0x3d
	.4byte	.LASF387
	.byte	0x5
	.2byte	0x1c6
	.4byte	0x317
	.byte	0
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF388
	.byte	0x5
	.2byte	0x185
	.4byte	0x64
	.byte	0x1
	.4byte	0x1e11
	.uleb128 0x3c
	.4byte	.LASF389
	.byte	0x5
	.2byte	0x185
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x5
	.2byte	0x187
	.4byte	0x30c
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x188
	.4byte	0x30c
	.uleb128 0x3e
	.ascii	"j\000"
	.byte	0x5
	.2byte	0x188
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF179
	.byte	0x5
	.2byte	0x189
	.4byte	0x30c
	.byte	0
	.uleb128 0x42
	.4byte	.LASF498
	.byte	0x5
	.2byte	0x17a
	.4byte	0x64
	.byte	0x1
	.uleb128 0x3a
	.4byte	.LASF390
	.byte	0x5
	.2byte	0x166
	.4byte	0x64
	.4byte	.LFB255
	.4byte	.LFE255-.LFB255
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1e7a
	.uleb128 0x43
	.4byte	.LASF179
	.byte	0x5
	.2byte	0x166
	.4byte	0x30c
	.4byte	.LLST78
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x168
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF391
	.byte	0x5
	.2byte	0x169
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL335
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL336
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL337
	.4byte	0x1e7a
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF392
	.byte	0x5
	.2byte	0x158
	.4byte	0x64
	.4byte	.LFB254
	.4byte	.LFE254-.LFB254
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1eae
	.uleb128 0x43
	.4byte	.LASF393
	.byte	0x5
	.2byte	0x158
	.4byte	0x30c
	.4byte	.LLST77
	.uleb128 0x24
	.4byte	.LVL333
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF394
	.byte	0x5
	.2byte	0x13b
	.4byte	0x317
	.4byte	.LFB253
	.4byte	.LFE253-.LFB253
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1f22
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x5
	.2byte	0x13d
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF381
	.byte	0x5
	.2byte	0x13e
	.4byte	0x1d4c
	.4byte	.LLST244
	.uleb128 0x24
	.4byte	.LVL1281
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1282
	.4byte	0x22d8
	.uleb128 0x24
	.4byte	.LVL1283
	.4byte	0x20d1
	.uleb128 0x24
	.4byte	.LVL1284
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1285
	.4byte	0x4994
	.uleb128 0x24
	.4byte	.LVL1286
	.4byte	0x20a0
	.uleb128 0x24
	.4byte	.LVL1288
	.4byte	0x21fa
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF395
	.byte	0x5
	.2byte	0x12b
	.4byte	0x317
	.4byte	.LFB252
	.4byte	.LFE252-.LFB252
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x1f7c
	.uleb128 0x24
	.4byte	.LVL1289
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1290
	.4byte	0x20d1
	.uleb128 0x24
	.4byte	.LVL1291
	.4byte	0x2512
	.uleb128 0x24
	.4byte	.LVL1292
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1293
	.4byte	0x3734
	.uleb128 0x24
	.4byte	.LVL1294
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1295
	.4byte	0x23f1
	.byte	0
	.uleb128 0x45
	.4byte	.LASF454
	.byte	0x5
	.2byte	0x120
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF396
	.byte	0x5
	.byte	0xd2
	.4byte	0x317
	.4byte	.LFB250
	.4byte	.LFE250-.LFB250
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x20a0
	.uleb128 0x23
	.4byte	.LASF397
	.byte	0x5
	.byte	0xd2
	.4byte	0x12cb
	.4byte	.LLST161
	.uleb128 0x23
	.4byte	.LASF398
	.byte	0x5
	.byte	0xd2
	.4byte	0x317
	.4byte	.LLST162
	.uleb128 0x2c
	.4byte	.LASF379
	.byte	0x5
	.byte	0xd4
	.4byte	0x30c
	.4byte	.LLST163
	.uleb128 0x2c
	.4byte	.LASF358
	.byte	0x5
	.byte	0xd5
	.4byte	0x30c
	.4byte	.LLST164
	.uleb128 0x29
	.ascii	"req\000"
	.byte	0x5
	.byte	0xd5
	.4byte	0x30c
	.uleb128 0x34
	.4byte	.LASF377
	.byte	0x5
	.byte	0xd6
	.4byte	0x30c
	.uleb128 0x2c
	.4byte	.LASF173
	.byte	0x5
	.byte	0xd6
	.4byte	0x30c
	.4byte	.LLST165
	.uleb128 0x2c
	.4byte	.LASF171
	.byte	0x5
	.byte	0xd6
	.4byte	0x30c
	.4byte	.LLST166
	.uleb128 0x2c
	.4byte	.LASF399
	.byte	0x5
	.byte	0xd7
	.4byte	0x317
	.4byte	.LLST167
	.uleb128 0x2c
	.4byte	.LASF400
	.byte	0x5
	.byte	0xd8
	.4byte	0x317
	.4byte	.LLST168
	.uleb128 0x34
	.4byte	.LASF401
	.byte	0x5
	.byte	0xd9
	.4byte	0x317
	.uleb128 0x2c
	.4byte	.LASF381
	.byte	0x5
	.byte	0xda
	.4byte	0x1d4c
	.4byte	.LLST169
	.uleb128 0x46
	.4byte	.LASF402
	.byte	0x5
	.byte	0xe1
	.uleb128 0x46
	.4byte	.LASF403
	.byte	0x5
	.byte	0xe5
	.uleb128 0x47
	.4byte	.LASF472
	.byte	0x5
	.2byte	0x117
	.4byte	.L960
	.uleb128 0x24
	.4byte	.LVL797
	.4byte	0x20a0
	.uleb128 0x24
	.4byte	.LVL804
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL818
	.4byte	0x458e
	.uleb128 0x24
	.4byte	.LVL825
	.4byte	0x3147
	.uleb128 0x24
	.4byte	.LVL827
	.4byte	0x21fa
	.uleb128 0x24
	.4byte	.LVL829
	.4byte	0x4228
	.uleb128 0x24
	.4byte	.LVL830
	.4byte	0x20a0
	.byte	0
	.uleb128 0x48
	.4byte	.LASF410
	.byte	0x5
	.byte	0xc9
	.4byte	.LFB249
	.4byte	.LFE249-.LFB249
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x20d1
	.uleb128 0x24
	.4byte	.LVL791
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL792
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL793
	.4byte	0x231c
	.byte	0
	.uleb128 0x22
	.4byte	.LASF404
	.byte	0x5
	.byte	0x6f
	.4byte	0x64
	.4byte	.LFB248
	.4byte	.LFE248-.LFB248
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x21e5
	.uleb128 0x23
	.4byte	.LASF405
	.byte	0x5
	.byte	0x6f
	.4byte	0x64
	.4byte	.LLST240
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x71
	.4byte	0x30c
	.uleb128 0x34
	.4byte	.LASF406
	.byte	0x5
	.byte	0x71
	.4byte	0x30c
	.uleb128 0x2c
	.4byte	.LASF407
	.byte	0x5
	.byte	0x72
	.4byte	0x317
	.4byte	.LLST241
	.uleb128 0x2c
	.4byte	.LASF408
	.byte	0x5
	.byte	0x73
	.4byte	0x317
	.4byte	.LLST242
	.uleb128 0x34
	.4byte	.LASF397
	.byte	0x5
	.byte	0x74
	.4byte	0x12cb
	.uleb128 0x2c
	.4byte	.LASF409
	.byte	0x5
	.byte	0x75
	.4byte	0x1304
	.4byte	.LLST243
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x21f5
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7458
	.uleb128 0x24
	.4byte	.LVL1248
	.4byte	0x1f85
	.uleb128 0x24
	.4byte	.LVL1250
	.4byte	0x3734
	.uleb128 0x24
	.4byte	.LVL1251
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1252
	.4byte	0x4994
	.uleb128 0x24
	.4byte	.LVL1256
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1257
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1259
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1261
	.4byte	0x4203
	.uleb128 0x24
	.4byte	.LVL1262
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1263
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1265
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1266
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1267
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1268
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1269
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1271
	.4byte	0x4228
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x21f5
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x12
	.byte	0
	.uleb128 0x4
	.4byte	0x21e5
	.uleb128 0x48
	.4byte	.LASF411
	.byte	0x5
	.byte	0x54
	.4byte	.LFB247
	.4byte	.LFE247-.LFB247
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2262
	.uleb128 0x23
	.4byte	.LASF412
	.byte	0x5
	.byte	0x54
	.4byte	0x317
	.4byte	.LLST72
	.uleb128 0x23
	.4byte	.LASF182
	.byte	0x5
	.byte	0x54
	.4byte	0x317
	.4byte	.LLST73
	.uleb128 0x2b
	.ascii	"lpa\000"
	.byte	0x5
	.byte	0x54
	.4byte	0x317
	.4byte	.LLST74
	.uleb128 0x2c
	.4byte	.LASF413
	.byte	0x5
	.byte	0x56
	.4byte	0x30c
	.4byte	.LLST75
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x5
	.byte	0x57
	.4byte	0x30c
	.4byte	.LLST76
	.uleb128 0x24
	.4byte	.LVL322
	.4byte	0x455a
	.byte	0
	.uleb128 0x22
	.4byte	.LASF414
	.byte	0x5
	.byte	0x49
	.4byte	0x317
	.4byte	.LFB246
	.4byte	.LFE246-.LFB246
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2294
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x5
	.byte	0x49
	.4byte	0x30c
	.4byte	.LLST71
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x4b
	.4byte	0x30c
	.byte	0
	.uleb128 0x48
	.4byte	.LASF415
	.byte	0x5
	.byte	0x38
	.4byte	.LFB245
	.4byte	.LFE245-.LFB245
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x22d8
	.uleb128 0x4a
	.ascii	"req\000"
	.byte	0x5
	.byte	0x38
	.4byte	0xf4b
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x4b
	.4byte	.LASF416
	.byte	0x5
	.byte	0x38
	.4byte	0x317
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x3a
	.4byte	0x30c
	.uleb128 0x34
	.4byte	.LASF417
	.byte	0x5
	.byte	0x3b
	.4byte	0x30c
	.byte	0
	.uleb128 0x48
	.4byte	.LASF418
	.byte	0x5
	.byte	0x29
	.4byte	.LFB244
	.4byte	.LFE244-.LFB244
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x231c
	.uleb128 0x4a
	.ascii	"req\000"
	.byte	0x5
	.byte	0x29
	.4byte	0xf4b
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x4b
	.4byte	.LASF416
	.byte	0x5
	.byte	0x29
	.4byte	0x317
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x2b
	.4byte	0x30c
	.uleb128 0x34
	.4byte	.LASF417
	.byte	0x5
	.byte	0x2c
	.4byte	0x30c
	.byte	0
	.uleb128 0x48
	.4byte	.LASF419
	.byte	0x5
	.byte	0x15
	.4byte	.LFB243
	.4byte	.LFE243-.LFB243
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x233b
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x5
	.byte	0x17
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF420
	.byte	0x2
	.2byte	0xa93
	.4byte	0x64
	.4byte	.LFB242
	.4byte	.LFE242-.LFB242
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x239c
	.uleb128 0x43
	.4byte	.LASF406
	.byte	0x2
	.2byte	0xa93
	.4byte	0x30c
	.4byte	.LLST185
	.uleb128 0x3e
	.ascii	"ret\000"
	.byte	0x2
	.2byte	0xa95
	.4byte	0x64
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x23ac
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7388
	.uleb128 0x24
	.4byte	.LVL911
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL912
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL914
	.4byte	0x23b1
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x23ac
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x13
	.byte	0
	.uleb128 0x4
	.4byte	0x239c
	.uleb128 0x3b
	.4byte	.LASF421
	.byte	0x2
	.2byte	0xa7e
	.4byte	0x64
	.byte	0x1
	.4byte	0x23dc
	.uleb128 0x3c
	.4byte	.LASF406
	.byte	0x2
	.2byte	0xa7e
	.4byte	0x30c
	.uleb128 0x40
	.4byte	.LASF363
	.4byte	0x23ec
	.4byte	.LASF421
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x23ec
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xf
	.byte	0
	.uleb128 0x4
	.4byte	0x23dc
	.uleb128 0x3a
	.4byte	.LASF422
	.byte	0x2
	.2byte	0xa5e
	.4byte	0x317
	.4byte	.LFB240
	.4byte	.LFE240-.LFB240
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x246d
	.uleb128 0x43
	.4byte	.LASF423
	.byte	0x2
	.2byte	0xa5e
	.4byte	0x12cb
	.4byte	.LLST69
	.uleb128 0x44
	.4byte	.LASF379
	.byte	0x2
	.2byte	0xa60
	.4byte	0x30c
	.4byte	.LLST70
	.uleb128 0x1f
	.4byte	.LASF424
	.byte	0x2
	.2byte	0xa61
	.4byte	0x317
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x21f5
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7368
	.uleb128 0x24
	.4byte	.LVL307
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL308
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL309
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL312
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF425
	.byte	0x2
	.2byte	0xa46
	.4byte	0x64
	.4byte	.LFB239
	.4byte	.LFE239-.LFB239
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x24fd
	.uleb128 0x43
	.4byte	.LASF423
	.byte	0x2
	.2byte	0xa46
	.4byte	0x12cb
	.4byte	.LLST226
	.uleb128 0x1f
	.4byte	.LASF406
	.byte	0x2
	.2byte	0xa48
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x250d
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7361
	.uleb128 0x24
	.4byte	.LVL1183
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1184
	.4byte	0x4203
	.uleb128 0x24
	.4byte	.LVL1185
	.4byte	0x23b1
	.uleb128 0x24
	.4byte	.LVL1186
	.4byte	0x2512
	.uleb128 0x24
	.4byte	.LVL1187
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1188
	.4byte	0x3734
	.uleb128 0x24
	.4byte	.LVL1189
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1190
	.4byte	0x4228
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x250d
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1c
	.byte	0
	.uleb128 0x4
	.4byte	0x24fd
	.uleb128 0x4c
	.4byte	.LASF426
	.byte	0x2
	.2byte	0x9e2
	.4byte	0x64
	.4byte	.LFB238
	.4byte	.LFE238-.LFB238
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x266c
	.uleb128 0x4d
	.ascii	"p\000"
	.byte	0x2
	.2byte	0x9e2
	.4byte	0x12cb
	.4byte	.LLST177
	.uleb128 0x44
	.4byte	.LASF427
	.byte	0x2
	.2byte	0x9e4
	.4byte	0x30c
	.4byte	.LLST178
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF358
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x30c
	.4byte	.LLST179
	.uleb128 0x3e
	.ascii	"n\000"
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x2
	.2byte	0x9e5
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF428
	.byte	0x2
	.2byte	0x9e6
	.4byte	0x64
	.4byte	.LLST180
	.uleb128 0x44
	.4byte	.LASF429
	.byte	0x2
	.2byte	0x9e7
	.4byte	0x30c
	.4byte	.LLST181
	.uleb128 0x3f
	.4byte	.LASF430
	.byte	0x2
	.2byte	0x9e8
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x267c
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7340
	.uleb128 0x4e
	.4byte	0x3414
	.4byte	.LBB429
	.4byte	.Ldebug_ranges0+0x178
	.byte	0x2
	.2byte	0xa18
	.4byte	0x25db
	.uleb128 0x2e
	.4byte	0x342d
	.uleb128 0x2e
	.4byte	0x3421
	.uleb128 0x24
	.4byte	.LVL882
	.4byte	0x5c7a
	.byte	0
	.uleb128 0x24
	.4byte	.LVL857
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL863
	.4byte	0x4026
	.uleb128 0x24
	.4byte	.LVL864
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL865
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL866
	.4byte	0x4203
	.uleb128 0x24
	.4byte	.LVL867
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL868
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL870
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL871
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL872
	.4byte	0x3414
	.uleb128 0x24
	.4byte	.LVL874
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL876
	.4byte	0x27b1
	.uleb128 0x24
	.4byte	.LVL877
	.4byte	0x4994
	.uleb128 0x24
	.4byte	.LVL879
	.4byte	0x4203
	.uleb128 0x24
	.4byte	.LVL884
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL887
	.4byte	0x6727
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x267c
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x18
	.byte	0
	.uleb128 0x4
	.4byte	0x266c
	.uleb128 0x3b
	.4byte	.LASF431
	.byte	0x2
	.2byte	0x9d9
	.4byte	0x64
	.byte	0x1
	.4byte	0x269f
	.uleb128 0x3c
	.4byte	.LASF406
	.byte	0x2
	.2byte	0x9d9
	.4byte	0x30c
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF432
	.byte	0x2
	.2byte	0x98e
	.4byte	0x30c
	.4byte	.LFB236
	.4byte	.LFE236-.LFB236
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2739
	.uleb128 0x1f
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x990
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x3d
	.4byte	.LASF434
	.byte	0x2
	.2byte	0x991
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x992
	.4byte	0x30c
	.4byte	.LLST64
	.uleb128 0x44
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x993
	.4byte	0x121b
	.4byte	.LLST65
	.uleb128 0x44
	.4byte	.LASF437
	.byte	0x2
	.2byte	0x994
	.4byte	0x30c
	.4byte	.LLST66
	.uleb128 0x44
	.4byte	.LASF438
	.byte	0x2
	.2byte	0x995
	.4byte	0x30c
	.4byte	.LLST67
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x996
	.4byte	0x317
	.uleb128 0x24
	.4byte	.LVL288
	.4byte	0x2739
	.uleb128 0x24
	.4byte	.LVL294
	.4byte	0x2788
	.uleb128 0x24
	.4byte	.LVL299
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF439
	.byte	0x2
	.2byte	0x979
	.4byte	0x30c
	.4byte	.LFB235
	.4byte	.LFE235-.LFB235
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2788
	.uleb128 0x43
	.4byte	.LASF94
	.byte	0x2
	.2byte	0x979
	.4byte	0x30c
	.4byte	.LLST59
	.uleb128 0x1f
	.4byte	.LASF440
	.byte	0x2
	.2byte	0x97b
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x3d
	.4byte	.LASF393
	.byte	0x2
	.2byte	0x97c
	.4byte	0x30c
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x97d
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF441
	.byte	0x2
	.2byte	0x970
	.4byte	0x30c
	.4byte	.LFB234
	.4byte	.LFE234-.LFB234
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x27b1
	.uleb128 0x1f
	.4byte	.LASF435
	.byte	0x2
	.2byte	0x972
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF442
	.byte	0x2
	.2byte	0x960
	.4byte	0x64
	.4byte	.LFB233
	.4byte	.LFE233-.LFB233
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2812
	.uleb128 0x43
	.4byte	.LASF406
	.byte	0x2
	.2byte	0x960
	.4byte	0x30c
	.4byte	.LLST58
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x962
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x2
	.2byte	0x962
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF166
	.byte	0x2
	.2byte	0x963
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL196
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL197
	.4byte	0x4af7
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF443
	.byte	0x2
	.2byte	0x901
	.4byte	0x64
	.byte	0x1
	.4byte	0x283c
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x903
	.4byte	0x64
	.uleb128 0x28
	.uleb128 0x3d
	.4byte	.LASF397
	.byte	0x2
	.2byte	0x928
	.4byte	0x12cb
	.byte	0
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF444
	.byte	0x2
	.2byte	0x8e2
	.4byte	.LFB231
	.4byte	.LFE231-.LFB231
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x28a7
	.uleb128 0x50
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8e4
	.4byte	0x64
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x50
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x8e5
	.4byte	0x317
	.uleb128 0x2
	.byte	0x91
	.sleb128 -36
	.uleb128 0x3d
	.4byte	.LASF445
	.byte	0x2
	.2byte	0x8e6
	.4byte	0x1d4c
	.uleb128 0x24
	.4byte	.LVL633
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL636
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL637
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL638
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL639
	.4byte	0x6727
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF446
	.byte	0x2
	.2byte	0x8ae
	.4byte	.LFB230
	.4byte	.LFE230-.LFB230
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x29c0
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x8b0
	.4byte	0x30c
	.uleb128 0x51
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x8b1
	.4byte	0x317
	.4byte	.LLST142
	.uleb128 0x1f
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x8b2
	.4byte	0x317
	.uleb128 0x2
	.byte	0x91
	.sleb128 -52
	.uleb128 0x3e
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x8b3
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF448
	.byte	0x2
	.2byte	0x8b4
	.4byte	0x30c
	.4byte	.LLST143
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x29d0
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7256
	.uleb128 0x52
	.4byte	.LBB390
	.4byte	.LBE390-.LBB390
	.4byte	0x2938
	.uleb128 0x44
	.4byte	.LASF94
	.byte	0x2
	.2byte	0x8d1
	.4byte	0x30c
	.4byte	.LLST144
	.uleb128 0x24
	.4byte	.LVL652
	.4byte	0x6727
	.byte	0
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB387
	.4byte	.LBE387-.LBB387
	.byte	0x2
	.2byte	0x8b7
	.4byte	0x2992
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB388
	.4byte	.LBE388-.LBB388
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB389
	.4byte	.LBE389-.LBB389
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL648
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL647
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL653
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL655
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL656
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL658
	.4byte	0x6727
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x29d0
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xd
	.byte	0
	.uleb128 0x4
	.4byte	0x29c0
	.uleb128 0x4f
	.4byte	.LASF449
	.byte	0x2
	.2byte	0x88f
	.4byte	.LFB229
	.4byte	.LFE229-.LFB229
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2a7c
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x891
	.4byte	0x30c
	.4byte	.LLST224
	.uleb128 0x51
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x892
	.4byte	0x317
	.4byte	.LLST225
	.uleb128 0x1f
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x893
	.4byte	0x317
	.uleb128 0x2
	.byte	0x91
	.sleb128 -44
	.uleb128 0x3e
	.ascii	"blk\000"
	.byte	0x2
	.2byte	0x894
	.4byte	0x30c
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x2a8c
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7240
	.uleb128 0x24
	.4byte	.LVL1169
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1170
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1174
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1175
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1177
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1178
	.4byte	0x23b1
	.uleb128 0x24
	.4byte	.LVL1179
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1180
	.4byte	0x3593
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2a8c
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x14
	.byte	0
	.uleb128 0x4
	.4byte	0x2a7c
	.uleb128 0x4f
	.4byte	.LASF450
	.byte	0x2
	.2byte	0x86b
	.4byte	.LFB228
	.4byte	.LFE228-.LFB228
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b06
	.uleb128 0x43
	.4byte	.LASF397
	.byte	0x2
	.2byte	0x86b
	.4byte	0x12cb
	.4byte	.LLST205
	.uleb128 0x1f
	.4byte	.LASF451
	.byte	0x2
	.2byte	0x86d
	.4byte	0x338
	.uleb128 0x2
	.byte	0x91
	.sleb128 -52
	.uleb128 0x3d
	.4byte	.LASF381
	.byte	0x2
	.2byte	0x86e
	.4byte	0x1d4c
	.uleb128 0x3d
	.4byte	.LASF452
	.byte	0x2
	.2byte	0x86f
	.4byte	0x64
	.uleb128 0x3d
	.4byte	.LASF175
	.byte	0x2
	.2byte	0x870
	.4byte	0x317
	.uleb128 0x24
	.4byte	.LVL1048
	.4byte	0x23f1
	.uleb128 0x24
	.4byte	.LVL1049
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1050
	.4byte	0x233b
	.byte	0
	.uleb128 0x54
	.4byte	.LASF453
	.byte	0x2
	.2byte	0x851
	.4byte	.LFB227
	.4byte	.LFE227-.LFB227
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2b80
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB487
	.4byte	.LBE487-.LBB487
	.byte	0x2
	.2byte	0x855
	.4byte	0x2b76
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB488
	.4byte	.LBE488-.LBB488
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB489
	.4byte	.LBE489-.LBB489
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL1152
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1151
	.4byte	0x3781
	.byte	0
	.uleb128 0x45
	.4byte	.LASF455
	.byte	0x2
	.2byte	0x83d
	.byte	0x1
	.uleb128 0x4c
	.4byte	.LASF456
	.byte	0x2
	.2byte	0x812
	.4byte	0x64
	.4byte	.LFB225
	.4byte	.LFE225-.LFB225
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2c3d
	.uleb128 0x44
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x814
	.4byte	0x30c
	.4byte	.LLST158
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x814
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x2
	.2byte	0x814
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF166
	.byte	0x2
	.2byte	0x814
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF457
	.byte	0x2
	.2byte	0x815
	.4byte	0x30c
	.4byte	.LLST159
	.uleb128 0x44
	.4byte	.LASF458
	.byte	0x2
	.2byte	0x815
	.4byte	0x30c
	.4byte	.LLST160
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x2c4d
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7215
	.uleb128 0x24
	.4byte	.LVL777
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL779
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL780
	.4byte	0x4af7
	.uleb128 0x24
	.4byte	.LVL784
	.4byte	0x4228
	.uleb128 0x24
	.4byte	.LVL787
	.4byte	0x4203
	.uleb128 0x24
	.4byte	.LVL789
	.4byte	0x6727
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x2c4d
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x11
	.byte	0
	.uleb128 0x4
	.4byte	0x2c3d
	.uleb128 0x3a
	.4byte	.LASF459
	.byte	0x2
	.2byte	0x7fc
	.4byte	0x64
	.4byte	.LFB224
	.4byte	.LFE224-.LFB224
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2cbd
	.uleb128 0x4d
	.ascii	"p\000"
	.byte	0x2
	.2byte	0x7fc
	.4byte	0x12cb
	.4byte	.LLST57
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x7fe
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x2
	.2byte	0x7fe
	.4byte	0x30c
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x23ec
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7194
	.uleb128 0x24
	.4byte	.LVL189
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL191
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL192
	.4byte	0x4af7
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF460
	.byte	0x2
	.2byte	0x7ef
	.4byte	0x64
	.4byte	.LFB223
	.4byte	.LFE223-.LFB223
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x2d0e
	.uleb128 0x24
	.4byte	.LVL1275
	.4byte	0x2d34
	.uleb128 0x24
	.4byte	.LVL1276
	.4byte	0x2d0e
	.uleb128 0x24
	.4byte	.LVL1277
	.4byte	0x2d34
	.uleb128 0x24
	.4byte	.LVL1278
	.4byte	0x2d0e
	.uleb128 0x24
	.4byte	.LVL1279
	.4byte	0x1f7c
	.uleb128 0x24
	.4byte	.LVL1280
	.4byte	0x233b
	.byte	0
	.uleb128 0x55
	.4byte	.LASF496
	.byte	0x2
	.2byte	0x7dc
	.byte	0x1
	.4byte	0x2d34
	.uleb128 0x3c
	.4byte	.LASF423
	.byte	0x2
	.2byte	0x7dc
	.4byte	0x12cb
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x2
	.2byte	0x7de
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF461
	.byte	0x2
	.2byte	0x677
	.4byte	0x64
	.4byte	.LFB221
	.4byte	.LFE221-.LFB221
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x307b
	.uleb128 0x43
	.4byte	.LASF397
	.byte	0x2
	.2byte	0x677
	.4byte	0x12cb
	.4byte	.LLST190
	.uleb128 0x44
	.4byte	.LASF462
	.byte	0x2
	.2byte	0x679
	.4byte	0x30c
	.4byte	.LLST191
	.uleb128 0x44
	.4byte	.LASF463
	.byte	0x2
	.2byte	0x679
	.4byte	0x30c
	.4byte	.LLST192
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x67a
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF173
	.byte	0x2
	.2byte	0x67a
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF171
	.byte	0x2
	.2byte	0x67a
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF464
	.byte	0x2
	.2byte	0x67b
	.4byte	0x30c
	.4byte	.LLST193
	.uleb128 0x44
	.4byte	.LASF465
	.byte	0x2
	.2byte	0x67b
	.4byte	0x30c
	.4byte	.LLST194
	.uleb128 0x3d
	.4byte	.LASF379
	.byte	0x2
	.2byte	0x67c
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF466
	.byte	0x2
	.2byte	0x67d
	.4byte	0x64
	.4byte	.LLST195
	.uleb128 0x3d
	.4byte	.LASF467
	.byte	0x2
	.2byte	0x67d
	.4byte	0x64
	.uleb128 0x3d
	.4byte	.LASF358
	.byte	0x2
	.2byte	0x67e
	.4byte	0x30c
	.uleb128 0x51
	.ascii	"n\000"
	.byte	0x2
	.2byte	0x67e
	.4byte	0x30c
	.4byte	.LLST196
	.uleb128 0x3e
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x67e
	.4byte	0x30c
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x67f
	.4byte	0x317
	.4byte	.LLST197
	.uleb128 0x44
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x67f
	.4byte	0x317
	.4byte	.LLST198
	.uleb128 0x44
	.4byte	.LASF407
	.byte	0x2
	.2byte	0x67f
	.4byte	0x317
	.4byte	.LLST199
	.uleb128 0x44
	.4byte	.LASF399
	.byte	0x2
	.2byte	0x67f
	.4byte	0x317
	.4byte	.LLST200
	.uleb128 0x44
	.4byte	.LASF413
	.byte	0x2
	.2byte	0x680
	.4byte	0x30c
	.4byte	.LLST201
	.uleb128 0x3d
	.4byte	.LASF468
	.byte	0x2
	.2byte	0x680
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF469
	.byte	0x2
	.2byte	0x681
	.4byte	0x317
	.4byte	.LLST202
	.uleb128 0x3d
	.4byte	.LASF470
	.byte	0x2
	.2byte	0x682
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF381
	.byte	0x2
	.2byte	0x683
	.4byte	0x1d4c
	.uleb128 0x3d
	.4byte	.LASF471
	.byte	0x2
	.2byte	0x684
	.4byte	0x317
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x308b
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7137
	.uleb128 0x47
	.4byte	.LASF473
	.byte	0x2
	.2byte	0x713
	.4byte	.L1118
	.uleb128 0x47
	.4byte	.LASF403
	.byte	0x2
	.2byte	0x71b
	.4byte	.L1130
	.uleb128 0x47
	.4byte	.LASF474
	.byte	0x2
	.2byte	0x7ce
	.4byte	.L1136
	.uleb128 0x3f
	.4byte	.LASF475
	.byte	0x2
	.2byte	0x7d8
	.uleb128 0x52
	.4byte	.LBB450
	.4byte	.LBE450-.LBB450
	.4byte	0x2f0a
	.uleb128 0x3d
	.4byte	.LASF476
	.byte	0x2
	.2byte	0x6eb
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF477
	.byte	0x2
	.2byte	0x6ec
	.4byte	0x317
	.uleb128 0x24
	.4byte	.LVL975
	.4byte	0x1b05
	.byte	0
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x1c0
	.4byte	0x2f7c
	.uleb128 0x44
	.4byte	.LASF478
	.byte	0x2
	.2byte	0x746
	.4byte	0x317
	.4byte	.LLST203
	.uleb128 0x44
	.4byte	.LASF479
	.byte	0x2
	.2byte	0x747
	.4byte	0x317
	.4byte	.LLST204
	.uleb128 0x24
	.4byte	.LVL1013
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1015
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1020
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1022
	.4byte	0x4517
	.uleb128 0x24
	.4byte	.LVL1024
	.4byte	0x3090
	.uleb128 0x24
	.4byte	.LVL1026
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1028
	.4byte	0x4517
	.uleb128 0x24
	.4byte	.LVL1029
	.4byte	0x4517
	.byte	0
	.uleb128 0x52
	.4byte	.LBB454
	.4byte	.LBE454-.LBB454
	.4byte	0x2fbd
	.uleb128 0x3d
	.4byte	.LASF480
	.byte	0x2
	.2byte	0x785
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF479
	.byte	0x2
	.2byte	0x786
	.4byte	0x317
	.uleb128 0x24
	.4byte	.LVL1033
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1034
	.4byte	0x4517
	.uleb128 0x24
	.4byte	.LVL1035
	.4byte	0x392c
	.byte	0
	.uleb128 0x24
	.4byte	.LVL941
	.4byte	0x447f
	.uleb128 0x24
	.4byte	.LVL943
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL949
	.4byte	0x458e
	.uleb128 0x24
	.4byte	.LVL955
	.4byte	0x3147
	.uleb128 0x24
	.4byte	.LVL958
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL960
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL964
	.4byte	0x4517
	.uleb128 0x24
	.4byte	.LVL979
	.4byte	0x36a4
	.uleb128 0x24
	.4byte	.LVL982
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL991
	.4byte	0x458e
	.uleb128 0x24
	.4byte	.LVL1000
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1004
	.4byte	0x4517
	.uleb128 0x24
	.4byte	.LVL1009
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1011
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1012
	.4byte	0x4517
	.uleb128 0x24
	.4byte	.LVL1016
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1018
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1031
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1032
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1037
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1039
	.4byte	0x233b
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x308b
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x15
	.byte	0
	.uleb128 0x4
	.4byte	0x307b
	.uleb128 0x4f
	.4byte	.LASF481
	.byte	0x2
	.2byte	0x65b
	.4byte	.LFB220
	.4byte	.LFE220-.LFB220
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3147
	.uleb128 0x4d
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x65b
	.4byte	0x317
	.4byte	.LLST136
	.uleb128 0x43
	.4byte	.LASF447
	.byte	0x2
	.2byte	0x65b
	.4byte	0x317
	.4byte	.LLST137
	.uleb128 0x44
	.4byte	.LASF413
	.byte	0x2
	.2byte	0x65d
	.4byte	0x30c
	.4byte	.LLST138
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x23ec
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7103
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x128
	.4byte	0x3134
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x661
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF393
	.byte	0x2
	.2byte	0x662
	.4byte	0x30c
	.4byte	.LLST139
	.uleb128 0x44
	.4byte	.LASF94
	.byte	0x2
	.2byte	0x663
	.4byte	0x30c
	.4byte	.LLST140
	.uleb128 0x24
	.4byte	.LVL621
	.4byte	0x4086
	.uleb128 0x24
	.4byte	.LVL622
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL623
	.4byte	0x4203
	.byte	0
	.uleb128 0x24
	.4byte	.LVL615
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL630
	.4byte	0x392c
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF482
	.byte	0x2
	.2byte	0x647
	.4byte	.LFB219
	.4byte	.LFE219-.LFB219
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3198
	.uleb128 0x56
	.4byte	.LASF397
	.byte	0x2
	.2byte	0x647
	.4byte	0x12cb
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x43
	.4byte	.LASF171
	.byte	0x2
	.2byte	0x647
	.4byte	0x30c
	.4byte	.LLST55
	.uleb128 0x43
	.4byte	.LASF173
	.byte	0x2
	.2byte	0x647
	.4byte	0x30c
	.4byte	.LLST56
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x649
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF483
	.byte	0x2
	.2byte	0x63e
	.4byte	0x64
	.4byte	.LFB218
	.4byte	.LFE218-.LFB218
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31c5
	.uleb128 0x24
	.4byte	.LVL766
	.4byte	0x31c5
	.uleb128 0x24
	.4byte	.LVL767
	.4byte	0x321e
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF484
	.byte	0x2
	.2byte	0x627
	.4byte	.LFB217
	.4byte	.LFE217-.LFB217
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x31fa
	.uleb128 0x3d
	.4byte	.LASF120
	.byte	0x2
	.2byte	0x629
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL764
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL765
	.4byte	0x17d1
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF485
	.byte	0x2
	.2byte	0x617
	.4byte	0x64
	.4byte	.LFB216
	.4byte	.LFE216-.LFB216
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x321e
	.uleb128 0x24
	.4byte	.LVL763
	.4byte	0x321e
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF486
	.byte	0x2
	.2byte	0x5b9
	.4byte	0x64
	.4byte	.LFB215
	.4byte	.LFE215-.LFB215
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x332e
	.uleb128 0x43
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x5b9
	.4byte	0x332e
	.4byte	.LLST150
	.uleb128 0x44
	.4byte	.LASF488
	.byte	0x2
	.2byte	0x5bb
	.4byte	0x32d
	.4byte	.LLST151
	.uleb128 0x3d
	.4byte	.LASF86
	.byte	0x2
	.2byte	0x5bb
	.4byte	0x32d
	.uleb128 0x44
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x5bc
	.4byte	0x32d
	.4byte	.LLST152
	.uleb128 0x3d
	.4byte	.LASF120
	.byte	0x2
	.2byte	0x5bd
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x5be
	.4byte	0x544
	.4byte	.LLST153
	.uleb128 0x44
	.4byte	.LASF99
	.byte	0x2
	.2byte	0x5bf
	.4byte	0x63b
	.4byte	.LLST154
	.uleb128 0x44
	.4byte	.LASF100
	.byte	0x2
	.2byte	0x5c0
	.4byte	0x544
	.4byte	.LLST155
	.uleb128 0x44
	.4byte	.LASF96
	.byte	0x2
	.2byte	0x5c1
	.4byte	0x30c
	.4byte	.LLST156
	.uleb128 0x44
	.4byte	.LASF491
	.byte	0x2
	.2byte	0x5c2
	.4byte	0x30c
	.4byte	.LLST157
	.uleb128 0x3d
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x5c3
	.4byte	0x3334
	.uleb128 0x24
	.4byte	.LVL716
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL724
	.4byte	0x447f
	.uleb128 0x24
	.4byte	.LVL734
	.4byte	0x3dce
	.uleb128 0x24
	.4byte	.LVL735
	.4byte	0x3d76
	.uleb128 0x24
	.4byte	.LVL736
	.4byte	0x3cab
	.uleb128 0x24
	.4byte	.LVL737
	.4byte	0x3cab
	.uleb128 0x24
	.4byte	.LVL741
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL750
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL754
	.4byte	0x1b05
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x592
	.uleb128 0x8
	.byte	0x4
	.4byte	0x7c3
	.uleb128 0x4c
	.4byte	.LASF493
	.byte	0x2
	.2byte	0x54f
	.4byte	0x64
	.4byte	.LFB214
	.4byte	.LFE214-.LFB214
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x33e9
	.uleb128 0x44
	.4byte	.LASF489
	.byte	0x2
	.2byte	0x551
	.4byte	0x32d
	.4byte	.LLST286
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x1b00
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.7040
	.uleb128 0x24
	.4byte	.LVL1561
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1562
	.4byte	0x447f
	.uleb128 0x24
	.4byte	.LVL1564
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1565
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1567
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1568
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1569
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1571
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1572
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1573
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1574
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1575
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1577
	.4byte	0x1b05
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF494
	.byte	0x2
	.2byte	0x54a
	.4byte	0x64
	.4byte	.LFB213
	.4byte	.LFE213-.LFB213
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3414
	.uleb128 0x43
	.4byte	.LASF495
	.byte	0x2
	.2byte	0x54a
	.4byte	0x30c
	.4byte	.LLST54
	.byte	0
	.uleb128 0x55
	.4byte	.LASF497
	.byte	0x2
	.2byte	0x542
	.byte	0x1
	.4byte	0x343a
	.uleb128 0x3c
	.4byte	.LASF495
	.byte	0x2
	.2byte	0x542
	.4byte	0x30c
	.uleb128 0x3c
	.4byte	.LASF373
	.byte	0x2
	.2byte	0x542
	.4byte	0x64
	.byte	0
	.uleb128 0x42
	.4byte	.LASF499
	.byte	0x2
	.2byte	0x535
	.4byte	0x64
	.byte	0x1
	.uleb128 0x3a
	.4byte	.LASF500
	.byte	0x2
	.2byte	0x446
	.4byte	0x64
	.4byte	.LFB210
	.4byte	.LFE210-.LFB210
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3593
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x2
	.2byte	0x448
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF501
	.byte	0x2
	.2byte	0x448
	.4byte	0x30c
	.4byte	.LLST147
	.uleb128 0x3d
	.4byte	.LASF179
	.byte	0x2
	.2byte	0x449
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF358
	.byte	0x2
	.2byte	0x449
	.4byte	0x30c
	.4byte	.LLST148
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x44a
	.4byte	0x32d
	.uleb128 0x3e
	.ascii	"j\000"
	.byte	0x2
	.2byte	0x44a
	.4byte	0x32d
	.uleb128 0x44
	.4byte	.LASF502
	.byte	0x2
	.2byte	0x44b
	.4byte	0x3334
	.4byte	.LLST149
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x29d0
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6983
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB396
	.4byte	.LBE396-.LBB396
	.byte	0x2
	.2byte	0x453
	.4byte	0x351d
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB397
	.4byte	.LBE397-.LBB397
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB398
	.4byte	.LBE398-.LBB398
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL668
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL669
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL670
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL671
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL678
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL679
	.4byte	0x4af7
	.uleb128 0x24
	.4byte	.LVL694
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL698
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL699
	.4byte	0x4367
	.uleb128 0x24
	.4byte	.LVL700
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL701
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL702
	.4byte	0x4367
	.uleb128 0x24
	.4byte	.LVL705
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF503
	.byte	0x2
	.2byte	0x3eb
	.4byte	0x64
	.4byte	.LFB209
	.4byte	.LFE209-.LFB209
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3652
	.uleb128 0x1f
	.4byte	.LASF87
	.byte	0x2
	.2byte	0x3ed
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x3d
	.4byte	.LASF504
	.byte	0x2
	.2byte	0x3ee
	.4byte	0x3652
	.uleb128 0x44
	.4byte	.LASF505
	.byte	0x2
	.2byte	0x3ef
	.4byte	0x30c
	.4byte	.LLST223
	.uleb128 0x3f
	.4byte	.LASF506
	.byte	0x2
	.2byte	0x41c
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x1b00
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6961
	.uleb128 0x24
	.4byte	.LVL1154
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1156
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1157
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1158
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1159
	.4byte	0x3710
	.uleb128 0x24
	.4byte	.LVL1160
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1161
	.4byte	0x424d
	.uleb128 0x24
	.4byte	.LVL1162
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1163
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1166
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1167
	.4byte	0x4367
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x81b
	.uleb128 0x55
	.4byte	.LASF507
	.byte	0x2
	.2byte	0x3b2
	.byte	0x1
	.4byte	0x36a4
	.uleb128 0x28
	.uleb128 0x3e
	.ascii	"lpa\000"
	.byte	0x2
	.2byte	0x3b7
	.4byte	0x317
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x3b8
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF508
	.byte	0x2
	.2byte	0x3b9
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF451
	.byte	0x2
	.2byte	0x3ba
	.4byte	0x338
	.uleb128 0x3d
	.4byte	.LASF381
	.byte	0x2
	.2byte	0x3bb
	.4byte	0x1d4c
	.byte	0
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF509
	.byte	0x2
	.2byte	0x380
	.4byte	.LFB207
	.4byte	.LFE207-.LFB207
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3710
	.uleb128 0x56
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x380
	.4byte	0x332e
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x2
	.2byte	0x382
	.4byte	0x317
	.4byte	.LLST145
	.uleb128 0x3d
	.4byte	.LASF106
	.byte	0x2
	.2byte	0x383
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x384
	.4byte	0x544
	.4byte	.LLST146
	.uleb128 0x24
	.4byte	.LVL663
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL664
	.4byte	0x3ac7
	.uleb128 0x24
	.4byte	.LVL667
	.4byte	0x17d1
	.byte	0
	.uleb128 0x55
	.4byte	.LASF510
	.byte	0x2
	.2byte	0x371
	.byte	0x1
	.4byte	0x3734
	.uleb128 0x3c
	.4byte	.LASF184
	.byte	0x2
	.2byte	0x371
	.4byte	0x317
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x373
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF511
	.byte	0x2
	.2byte	0x35c
	.4byte	0x64
	.4byte	.LFB205
	.4byte	.LFE205-.LFB205
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3781
	.uleb128 0x43
	.4byte	.LASF512
	.byte	0x2
	.2byte	0x35c
	.4byte	0x317
	.4byte	.LLST216
	.uleb128 0x44
	.4byte	.LASF513
	.byte	0x2
	.2byte	0x35e
	.4byte	0x317
	.4byte	.LLST217
	.uleb128 0x24
	.4byte	.LVL1134
	.4byte	0x3855
	.uleb128 0x24
	.4byte	.LVL1135
	.4byte	0x2b80
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF514
	.byte	0x2
	.2byte	0x336
	.4byte	0x317
	.4byte	.LFB204
	.4byte	.LFE204-.LFB204
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x384f
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x336
	.4byte	0x317
	.4byte	.LLST218
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x336
	.4byte	0x317
	.4byte	.LLST219
	.uleb128 0x43
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x336
	.4byte	0x384f
	.4byte	.LLST220
	.uleb128 0x51
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x338
	.4byte	0x30c
	.4byte	.LLST221
	.uleb128 0x3d
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x338
	.4byte	0x30c
	.uleb128 0x50
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x339
	.4byte	0x317
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x1f
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x33a
	.4byte	0x317
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x50
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x33b
	.4byte	0x338
	.uleb128 0x3
	.byte	0x91
	.sleb128 -132
	.uleb128 0x44
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x33c
	.4byte	0x317
	.4byte	.LLST222
	.uleb128 0x1f
	.4byte	.LASF354
	.byte	0x2
	.2byte	0x33d
	.4byte	0x194f
	.uleb128 0x3
	.byte	0x91
	.sleb128 -112
	.uleb128 0x24
	.4byte	.LVL1145
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1146
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1147
	.4byte	0x17d1
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x301
	.uleb128 0x4c
	.4byte	.LASF521
	.byte	0x2
	.2byte	0x311
	.4byte	0x317
	.4byte	.LFB203
	.4byte	.LFE203-.LFB203
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x392c
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x2
	.2byte	0x311
	.4byte	0x317
	.4byte	.LLST211
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x2
	.2byte	0x311
	.4byte	0x317
	.4byte	.LLST212
	.uleb128 0x43
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x311
	.4byte	0x384f
	.4byte	.LLST213
	.uleb128 0x51
	.ascii	"len\000"
	.byte	0x2
	.2byte	0x313
	.4byte	0x30c
	.4byte	.LLST214
	.uleb128 0x3d
	.4byte	.LASF518
	.byte	0x2
	.2byte	0x313
	.4byte	0x30c
	.uleb128 0x51
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x314
	.4byte	0x317
	.4byte	.LLST215
	.uleb128 0x1f
	.4byte	.LASF519
	.byte	0x2
	.2byte	0x315
	.4byte	0x317
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x50
	.ascii	"req\000"
	.byte	0x2
	.2byte	0x316
	.4byte	0x338
	.uleb128 0x3
	.byte	0x91
	.sleb128 -132
	.uleb128 0x1f
	.4byte	.LASF520
	.byte	0x2
	.2byte	0x317
	.4byte	0x317
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x1f
	.4byte	.LASF354
	.byte	0x2
	.2byte	0x318
	.4byte	0x194f
	.uleb128 0x3
	.byte	0x91
	.sleb128 -112
	.uleb128 0x24
	.4byte	.LVL1116
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1117
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1118
	.4byte	0x3ac7
	.uleb128 0x24
	.4byte	.LVL1123
	.4byte	0x17d1
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF522
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x64
	.4byte	.LFB202
	.4byte	.LFE202-.LFB202
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x39e2
	.uleb128 0x4d
	.ascii	"lpn\000"
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x317
	.4byte	.LLST132
	.uleb128 0x4d
	.ascii	"ppn\000"
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x544
	.4byte	.LLST133
	.uleb128 0x43
	.4byte	.LASF523
	.byte	0x2
	.2byte	0x2f0
	.4byte	0x64
	.4byte	.LLST134
	.uleb128 0x44
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x2f2
	.4byte	0x30c
	.4byte	.LLST135
	.uleb128 0x1f
	.4byte	.LASF525
	.byte	0x2
	.2byte	0x2f2
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x1f
	.4byte	.LASF526
	.byte	0x2
	.2byte	0x2f2
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x39f2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6888
	.uleb128 0x47
	.4byte	.LASF527
	.byte	0x2
	.2byte	0x2fb
	.4byte	.L728
	.uleb128 0x24
	.4byte	.LVL604
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL606
	.4byte	0x3a32
	.uleb128 0x24
	.4byte	.LVL607
	.4byte	0x3a87
	.uleb128 0x24
	.4byte	.LVL608
	.4byte	0x3e7f
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x39f2
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x8
	.byte	0
	.uleb128 0x4
	.4byte	0x39e2
	.uleb128 0x3a
	.4byte	.LASF528
	.byte	0x2
	.2byte	0x2e3
	.4byte	0x64
	.4byte	.LFB201
	.4byte	.LFE201-.LFB201
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a32
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x2e5
	.4byte	0x30c
	.4byte	.LLST189
	.uleb128 0x24
	.4byte	.LVL926
	.4byte	0x3658
	.uleb128 0x24
	.4byte	.LVL929
	.4byte	0x3a87
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF529
	.byte	0x2
	.2byte	0x2bb
	.4byte	0x30c
	.4byte	.LFB200
	.4byte	.LFE200-.LFB200
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3a87
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x2
	.2byte	0x2bd
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF530
	.byte	0x2
	.2byte	0x2be
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF531
	.byte	0x2
	.2byte	0x2bf
	.4byte	0x317
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x308b
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6871
	.uleb128 0x24
	.4byte	.LVL169
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF532
	.byte	0x2
	.2byte	0x2b1
	.4byte	0x64
	.4byte	.LFB199
	.4byte	.LFE199-.LFB199
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3ac7
	.uleb128 0x43
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x2b1
	.4byte	0x30c
	.4byte	.LLST131
	.uleb128 0x3d
	.4byte	.LASF106
	.byte	0x2
	.2byte	0x2b3
	.4byte	0x317
	.uleb128 0x24
	.4byte	.LVL599
	.4byte	0x3ac7
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF533
	.byte	0x2
	.2byte	0x276
	.4byte	0x64
	.4byte	.LFB198
	.4byte	.LFE198-.LFB198
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3bdb
	.uleb128 0x43
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x276
	.4byte	0x332e
	.4byte	.LLST125
	.uleb128 0x43
	.4byte	.LASF106
	.byte	0x2
	.2byte	0x276
	.4byte	0x317
	.4byte	.LLST126
	.uleb128 0x56
	.4byte	.LASF517
	.byte	0x2
	.2byte	0x276
	.4byte	0x544
	.uleb128 0x1
	.byte	0x52
	.uleb128 0x3d
	.4byte	.LASF179
	.byte	0x2
	.2byte	0x278
	.4byte	0x317
	.uleb128 0x44
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x279
	.4byte	0x3334
	.4byte	.LLST127
	.uleb128 0x44
	.4byte	.LASF505
	.byte	0x2
	.2byte	0x27a
	.4byte	0x30c
	.4byte	.LLST128
	.uleb128 0x3f
	.4byte	.LASF534
	.byte	0x2
	.2byte	0x27c
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x23ec
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6846
	.uleb128 0x4e
	.4byte	0x17d1
	.4byte	.LBB362
	.4byte	.Ldebug_ranges0+0x110
	.byte	0x2
	.2byte	0x292
	.4byte	0x3ba4
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x110
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x110
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL575
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL572
	.4byte	0x3bdb
	.uleb128 0x24
	.4byte	.LVL573
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL574
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL578
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL581
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL582
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF535
	.byte	0x2
	.2byte	0x246
	.4byte	0x64
	.4byte	.LFB197
	.4byte	.LFE197-.LFB197
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3c96
	.uleb128 0x43
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x246
	.4byte	0x332e
	.4byte	.LLST121
	.uleb128 0x3d
	.4byte	.LASF179
	.byte	0x2
	.2byte	0x248
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF120
	.byte	0x2
	.2byte	0x248
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF536
	.byte	0x2
	.2byte	0x249
	.4byte	0x30c
	.4byte	.LLST122
	.uleb128 0x44
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x24a
	.4byte	0x63b
	.4byte	.LLST123
	.uleb128 0x44
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x24b
	.4byte	0x544
	.4byte	.LLST124
	.uleb128 0x3d
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x24c
	.4byte	0x3334
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x3ca6
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6832
	.uleb128 0x24
	.4byte	.LVL555
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL556
	.4byte	0x424d
	.uleb128 0x24
	.4byte	.LVL562
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL565
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL566
	.4byte	0x3cab
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3ca6
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x1e
	.byte	0
	.uleb128 0x4
	.4byte	0x3c96
	.uleb128 0x3a
	.4byte	.LASF538
	.byte	0x2
	.2byte	0x218
	.4byte	0x64
	.4byte	.LFB196
	.4byte	.LFE196-.LFB196
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3d76
	.uleb128 0x43
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x218
	.4byte	0x332e
	.4byte	.LLST129
	.uleb128 0x1f
	.4byte	.LASF539
	.byte	0x2
	.2byte	0x21a
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x3d
	.4byte	.LASF540
	.byte	0x2
	.2byte	0x21b
	.4byte	0x30c
	.uleb128 0x1f
	.4byte	.LASF120
	.byte	0x2
	.2byte	0x21c
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x44
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x21d
	.4byte	0x63b
	.4byte	.LLST130
	.uleb128 0x1f
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x21e
	.4byte	0x544
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x3d
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x21f
	.4byte	0x3334
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x1b00
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6818
	.uleb128 0x24
	.4byte	.LVL586
	.4byte	0x3dce
	.uleb128 0x24
	.4byte	.LVL588
	.4byte	0x3d76
	.uleb128 0x24
	.4byte	.LVL590
	.4byte	0x4367
	.uleb128 0x24
	.4byte	.LVL592
	.4byte	0x3d76
	.uleb128 0x24
	.4byte	.LVL594
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL595
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL596
	.4byte	0x3ac7
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF541
	.byte	0x2
	.2byte	0x203
	.4byte	0x64
	.byte	0x1
	.4byte	0x3db9
	.uleb128 0x3c
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x203
	.4byte	0x332e
	.uleb128 0x3d
	.4byte	.LASF542
	.byte	0x2
	.2byte	0x205
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x206
	.4byte	0x63b
	.uleb128 0x40
	.4byte	.LASF363
	.4byte	0x3dc9
	.4byte	.LASF541
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x3dc9
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x19
	.byte	0
	.uleb128 0x4
	.4byte	0x3db9
	.uleb128 0x3a
	.4byte	.LASF543
	.byte	0x2
	.2byte	0x1db
	.4byte	0x64
	.4byte	.LFB194
	.4byte	.LFE194-.LFB194
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3e7f
	.uleb128 0x43
	.4byte	.LASF487
	.byte	0x2
	.2byte	0x1db
	.4byte	0x332e
	.4byte	.LLST116
	.uleb128 0x1f
	.4byte	.LASF537
	.byte	0x2
	.2byte	0x1dd
	.4byte	0x63b
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x44
	.4byte	.LASF490
	.byte	0x2
	.2byte	0x1de
	.4byte	0x544
	.4byte	.LLST117
	.uleb128 0x1f
	.4byte	.LASF544
	.byte	0x2
	.2byte	0x1df
	.4byte	0x63b
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x44
	.4byte	.LASF179
	.byte	0x2
	.2byte	0x1e0
	.4byte	0x30c
	.4byte	.LLST118
	.uleb128 0x3d
	.4byte	.LASF120
	.byte	0x2
	.2byte	0x1e0
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF542
	.byte	0x2
	.2byte	0x1e1
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF545
	.byte	0x2
	.2byte	0x1e2
	.4byte	0x30c
	.4byte	.LLST119
	.uleb128 0x44
	.4byte	.LASF540
	.byte	0x2
	.2byte	0x1e3
	.4byte	0x30c
	.4byte	.LLST120
	.uleb128 0x24
	.4byte	.LVL541
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL551
	.4byte	0x4367
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF546
	.byte	0x2
	.2byte	0x1b9
	.4byte	0x64
	.4byte	.LFB193
	.4byte	.LFE193-.LFB193
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3f26
	.uleb128 0x43
	.4byte	.LASF106
	.byte	0x2
	.2byte	0x1b9
	.4byte	0x30c
	.4byte	.LLST113
	.uleb128 0x43
	.4byte	.LASF524
	.byte	0x2
	.2byte	0x1b9
	.4byte	0x30c
	.4byte	.LLST114
	.uleb128 0x44
	.4byte	.LASF492
	.byte	0x2
	.2byte	0x1bb
	.4byte	0x3334
	.4byte	.LLST115
	.uleb128 0x1f
	.4byte	.LASF547
	.byte	0x2
	.2byte	0x1bc
	.4byte	0x317
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x23ec
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6775
	.uleb128 0x24
	.4byte	.LVL525
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL527
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL530
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL532
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL533
	.4byte	0x673d
	.uleb128 0x24
	.4byte	.LVL534
	.4byte	0x673d
	.uleb128 0x24
	.4byte	.LVL535
	.4byte	0x6727
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF548
	.byte	0x2
	.2byte	0x193
	.4byte	0x64
	.byte	0x1
	.4byte	0x3fa5
	.uleb128 0x3c
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x193
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF549
	.byte	0x2
	.2byte	0x195
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF550
	.byte	0x2
	.2byte	0x196
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF551
	.byte	0x2
	.2byte	0x197
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF552
	.byte	0x2
	.2byte	0x199
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF553
	.byte	0x2
	.2byte	0x19a
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x19b
	.4byte	0x121b
	.uleb128 0x3d
	.4byte	.LASF554
	.byte	0x2
	.2byte	0x19b
	.4byte	0x121b
	.uleb128 0x40
	.4byte	.LASF363
	.4byte	0x308b
	.4byte	.LASF548
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF555
	.byte	0x2
	.2byte	0x17f
	.4byte	0x64
	.4byte	.LFB191
	.4byte	.LFE191-.LFB191
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x3fec
	.uleb128 0x43
	.4byte	.LASF94
	.byte	0x2
	.2byte	0x17f
	.4byte	0x30c
	.4byte	.LLST46
	.uleb128 0x3d
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x181
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x182
	.4byte	0x121b
	.4byte	.LLST47
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF556
	.byte	0x2
	.2byte	0x17a
	.4byte	0x64
	.4byte	.LFB190
	.4byte	.LFE190-.LFB190
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4020
	.uleb128 0x43
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x17a
	.4byte	0x4020
	.4byte	.LLST45
	.uleb128 0x24
	.4byte	.LVL145
	.4byte	0x4026
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x121b
	.uleb128 0x3a
	.4byte	.LASF558
	.byte	0x2
	.2byte	0x167
	.4byte	0x64
	.4byte	.LFB189
	.4byte	.LFE189-.LFB189
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4086
	.uleb128 0x43
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x167
	.4byte	0x4020
	.4byte	.LLST42
	.uleb128 0x43
	.4byte	.LASF94
	.byte	0x2
	.2byte	0x167
	.4byte	0x30c
	.4byte	.LLST43
	.uleb128 0x3d
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x169
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x16a
	.4byte	0x121b
	.4byte	.LLST44
	.uleb128 0x24
	.4byte	.LVL139
	.4byte	0x4086
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF559
	.byte	0x2
	.2byte	0x14c
	.4byte	0x64
	.4byte	.LFB188
	.4byte	.LFE188-.LFB188
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x40f3
	.uleb128 0x43
	.4byte	.LASF557
	.byte	0x2
	.2byte	0x14c
	.4byte	0x4020
	.4byte	.LLST40
	.uleb128 0x43
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x14c
	.4byte	0x30c
	.4byte	.LLST41
	.uleb128 0x1f
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x14e
	.4byte	0x121b
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x3d
	.4byte	.LASF560
	.byte	0x2
	.2byte	0x14e
	.4byte	0x121b
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x4103
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6734
	.uleb128 0x24
	.4byte	.LVL131
	.4byte	0x6727
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x4103
	.uleb128 0x17
	.4byte	0x2e
	.byte	0x10
	.byte	0
	.uleb128 0x4
	.4byte	0x40f3
	.uleb128 0x3a
	.4byte	.LASF561
	.byte	0x2
	.2byte	0x11b
	.4byte	0x64
	.4byte	.LFB187
	.4byte	.LFE187-.LFB187
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4177
	.uleb128 0x43
	.4byte	.LASF433
	.byte	0x2
	.2byte	0x11b
	.4byte	0x30c
	.4byte	.LLST37
	.uleb128 0x44
	.4byte	.LASF436
	.byte	0x2
	.2byte	0x11d
	.4byte	0x121b
	.4byte	.LLST38
	.uleb128 0x1f
	.4byte	.LASF560
	.byte	0x2
	.2byte	0x11d
	.4byte	0x121b
	.uleb128 0x1
	.byte	0x53
	.uleb128 0x1f
	.4byte	.LASF562
	.byte	0x2
	.2byte	0x11e
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x3d
	.4byte	.LASF563
	.byte	0x2
	.2byte	0x11f
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF564
	.byte	0x2
	.2byte	0x120
	.4byte	0x30c
	.byte	0
	.uleb128 0x22
	.4byte	.LASF565
	.byte	0x2
	.byte	0xd8
	.4byte	0x64
	.4byte	.LFB186
	.4byte	.LFE186-.LFB186
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4203
	.uleb128 0x23
	.4byte	.LASF433
	.byte	0x2
	.byte	0xd8
	.4byte	0x30c
	.4byte	.LLST30
	.uleb128 0x2c
	.4byte	.LASF436
	.byte	0x2
	.byte	0xda
	.4byte	0x121b
	.4byte	.LLST31
	.uleb128 0x2c
	.4byte	.LASF560
	.byte	0x2
	.byte	0xda
	.4byte	0x121b
	.4byte	.LLST32
	.uleb128 0x2c
	.4byte	.LASF566
	.byte	0x2
	.byte	0xdb
	.4byte	0x30c
	.4byte	.LLST33
	.uleb128 0x20
	.4byte	.LASF567
	.byte	0x2
	.byte	0xdc
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x5a
	.uleb128 0x2c
	.4byte	.LASF551
	.byte	0x2
	.byte	0xdd
	.4byte	0x317
	.4byte	.LLST34
	.uleb128 0x2c
	.4byte	.LASF568
	.byte	0x2
	.byte	0xde
	.4byte	0x317
	.4byte	.LLST35
	.uleb128 0x34
	.4byte	.LASF564
	.byte	0x2
	.byte	0xdf
	.4byte	0x30c
	.byte	0
	.uleb128 0x57
	.4byte	.LASF569
	.byte	0x2
	.byte	0xb9
	.byte	0x1
	.4byte	0x4228
	.uleb128 0x33
	.4byte	.LASF433
	.byte	0x2
	.byte	0xb9
	.4byte	0x30c
	.uleb128 0x40
	.4byte	.LASF363
	.4byte	0x4103
	.4byte	.LASF569
	.byte	0
	.uleb128 0x57
	.4byte	.LASF570
	.byte	0x2
	.byte	0xb2
	.byte	0x1
	.4byte	0x424d
	.uleb128 0x33
	.4byte	.LASF433
	.byte	0x2
	.byte	0xb2
	.4byte	0x30c
	.uleb128 0x40
	.4byte	.LASF363
	.4byte	0x4103
	.4byte	.LASF570
	.byte	0
	.uleb128 0x22
	.4byte	.LASF571
	.byte	0x2
	.byte	0xa4
	.4byte	0x30c
	.4byte	.LFB183
	.4byte	.LFE183-.LFB183
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4296
	.uleb128 0x36
	.ascii	"tmp\000"
	.byte	0x2
	.byte	0xa6
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x58
	.4byte	0x4286
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0xa9
	.4byte	0x4296
	.byte	0
	.uleb128 0x58
	.4byte	0x43f4
	.4byte	.LBB252
	.4byte	.LBE252-.LBB252
	.byte	0x2
	.byte	0xa8
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0xae7
	.uleb128 0x22
	.4byte	.LASF572
	.byte	0x2
	.byte	0x92
	.4byte	0x64
	.4byte	.LFB182
	.4byte	.LFE182-.LFB182
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x42f5
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x92
	.4byte	0x30c
	.4byte	.LLST29
	.uleb128 0x36
	.ascii	"ret\000"
	.byte	0x2
	.byte	0x94
	.4byte	0x64
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x36
	.ascii	"i\000"
	.byte	0x2
	.byte	0x95
	.4byte	0x317
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x96
	.4byte	0x4296
	.uleb128 0x58
	.4byte	0x43e8
	.4byte	.LBB250
	.4byte	.LBE250-.LBB250
	.byte	0x2
	.byte	0x98
	.byte	0
	.uleb128 0x48
	.4byte	.LASF573
	.byte	0x2
	.byte	0x73
	.4byte	.LFB181
	.4byte	.LFE181-.LFB181
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4367
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x2
	.byte	0x75
	.4byte	0x30c
	.uleb128 0x49
	.ascii	"tmp\000"
	.byte	0x2
	.byte	0x75
	.4byte	0x30c
	.4byte	.LLST26
	.uleb128 0x29
	.ascii	"j\000"
	.byte	0x2
	.byte	0x75
	.4byte	0x30c
	.uleb128 0x2c
	.4byte	.LASF384
	.byte	0x2
	.byte	0x76
	.4byte	0x30c
	.4byte	.LLST27
	.uleb128 0x34
	.4byte	.LASF574
	.byte	0x2
	.byte	0x77
	.4byte	0x30c
	.uleb128 0x2c
	.4byte	.LASF575
	.byte	0x2
	.byte	0x78
	.4byte	0x317
	.4byte	.LLST28
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x79
	.4byte	0x4296
	.uleb128 0x24
	.4byte	.LVL76
	.4byte	0x455a
	.byte	0
	.uleb128 0x48
	.4byte	.LASF576
	.byte	0x2
	.byte	0x61
	.4byte	.LFB180
	.4byte	.LFE180-.LFB180
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x43e8
	.uleb128 0x2b
	.ascii	"blk\000"
	.byte	0x2
	.byte	0x61
	.4byte	0x30c
	.4byte	.LLST94
	.uleb128 0x23
	.4byte	.LASF577
	.byte	0x2
	.byte	0x61
	.4byte	0x30c
	.4byte	.LLST95
	.uleb128 0x52
	.4byte	.LBB299
	.4byte	.LBE299-.LBB299
	.4byte	0x43d8
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x64
	.4byte	0x4296
	.uleb128 0x32
	.4byte	.LBB300
	.4byte	.LBE300-.LBB300
	.uleb128 0x34
	.4byte	.LASF578
	.byte	0x2
	.byte	0x66
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL407
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL408
	.4byte	0x195f
	.byte	0
	.byte	0
	.uleb128 0x58
	.4byte	0x43e8
	.4byte	.LBB297
	.4byte	.LBE297-.LBB297
	.byte	0x2
	.byte	0x63
	.byte	0
	.uleb128 0x59
	.4byte	.LASF579
	.byte	0x2
	.byte	0x5c
	.4byte	0x30c
	.byte	0x1
	.uleb128 0x59
	.4byte	.LASF580
	.byte	0x2
	.byte	0x57
	.4byte	0x30c
	.byte	0x1
	.uleb128 0x22
	.4byte	.LASF581
	.byte	0x2
	.byte	0x4b
	.4byte	0x30c
	.4byte	.LFB177
	.4byte	.LFE177-.LFB177
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x447f
	.uleb128 0x2b
	.ascii	"max\000"
	.byte	0x2
	.byte	0x4b
	.4byte	0x30c
	.4byte	.LLST25
	.uleb128 0x29
	.ascii	"Q\000"
	.byte	0x2
	.byte	0x4d
	.4byte	0x4296
	.uleb128 0x5a
	.4byte	0x17d1
	.4byte	.LBB241
	.4byte	.Ldebug_ranges0+0x38
	.byte	0x2
	.byte	0x53
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x38
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x38
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.uleb128 0x24
	.4byte	.LVL71
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x22
	.4byte	.LASF582
	.byte	0x2
	.byte	0x2e
	.4byte	0x64
	.4byte	.LFB176
	.4byte	.LFE176-.LFB176
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4517
	.uleb128 0x23
	.4byte	.LASF179
	.byte	0x2
	.byte	0x2e
	.4byte	0x317
	.4byte	.LLST84
	.uleb128 0x23
	.4byte	.LASF357
	.byte	0x2
	.byte	0x2e
	.4byte	0x64
	.4byte	.LLST85
	.uleb128 0x36
	.ascii	"req\000"
	.byte	0x2
	.byte	0x30
	.4byte	0x338
	.uleb128 0x3
	.byte	0x91
	.sleb128 -124
	.uleb128 0x36
	.ascii	"low\000"
	.byte	0x2
	.byte	0x31
	.4byte	0x32d
	.uleb128 0x1
	.byte	0x58
	.uleb128 0x20
	.4byte	.LASF583
	.byte	0x2
	.byte	0x31
	.4byte	0x32d
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x29
	.ascii	"mid\000"
	.byte	0x2
	.byte	0x31
	.4byte	0x32d
	.uleb128 0x20
	.4byte	.LASF584
	.byte	0x2
	.byte	0x32
	.4byte	0x194f
	.uleb128 0x3
	.byte	0x91
	.sleb128 -104
	.uleb128 0x34
	.4byte	.LASF585
	.byte	0x2
	.byte	0x33
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL366
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL369
	.4byte	0x1b05
	.byte	0
	.uleb128 0x22
	.4byte	.LASF586
	.byte	0x2
	.byte	0x1e
	.4byte	0x317
	.4byte	.LFB175
	.4byte	.LFE175-.LFB175
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x455a
	.uleb128 0x23
	.4byte	.LASF587
	.byte	0x2
	.byte	0x1e
	.4byte	0x317
	.4byte	.LLST24
	.uleb128 0x4b
	.4byte	.LASF588
	.byte	0x2
	.byte	0x1e
	.4byte	0x317
	.uleb128 0x1
	.byte	0x51
	.uleb128 0x36
	.ascii	"ret\000"
	.byte	0x2
	.byte	0x20
	.4byte	0x317
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF589
	.byte	0x2
	.byte	0x14
	.4byte	0x30c
	.4byte	.LFB174
	.4byte	.LFE174-.LFB174
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x458e
	.uleb128 0x23
	.4byte	.LASF379
	.byte	0x2
	.byte	0x14
	.4byte	0x30c
	.4byte	.LLST23
	.uleb128 0x34
	.4byte	.LASF501
	.byte	0x2
	.byte	0x16
	.4byte	0x30c
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF590
	.byte	0x2
	.byte	0xc
	.4byte	0x30c
	.4byte	.LFB173
	.4byte	.LFE173-.LFB173
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x45c2
	.uleb128 0x23
	.4byte	.LASF379
	.byte	0x2
	.byte	0xc
	.4byte	0x30c
	.4byte	.LLST22
	.uleb128 0x34
	.4byte	.LASF377
	.byte	0x2
	.byte	0xe
	.4byte	0x30c
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF591
	.byte	0x2
	.byte	0x4
	.4byte	0x30c
	.4byte	.LFB172
	.4byte	.LFE172-.LFB172
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4605
	.uleb128 0x23
	.4byte	.LASF377
	.byte	0x2
	.byte	0x4
	.4byte	0x30c
	.4byte	.LLST20
	.uleb128 0x23
	.4byte	.LASF501
	.byte	0x2
	.byte	0x4
	.4byte	0x30c
	.4byte	.LLST21
	.uleb128 0x34
	.4byte	.LASF393
	.byte	0x2
	.byte	0x6
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF592
	.byte	0x4
	.2byte	0x13c
	.4byte	0x322
	.4byte	.LFB171
	.4byte	.LFE171-.LFB171
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x47c9
	.uleb128 0x51
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x13e
	.4byte	0x317
	.4byte	.LLST209
	.uleb128 0x3e
	.ascii	"blk\000"
	.byte	0x4
	.2byte	0x13f
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF353
	.byte	0x4
	.2byte	0x140
	.4byte	0x317
	.4byte	.LLST210
	.uleb128 0x3d
	.4byte	.LASF593
	.byte	0x4
	.2byte	0x142
	.4byte	0x47c9
	.uleb128 0x3f
	.4byte	.LASF594
	.byte	0x4
	.2byte	0x158
	.uleb128 0x4e
	.4byte	0x17d1
	.4byte	.LBB465
	.4byte	.Ldebug_ranges0+0x1e0
	.byte	0x4
	.2byte	0x15e
	.4byte	0x46b1
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1e0
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1e0
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL1088
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB471
	.4byte	.LBE471-.LBB471
	.byte	0x4
	.2byte	0x15f
	.4byte	0x470b
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB472
	.4byte	.LBE472-.LBB472
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB473
	.4byte	.LBE473-.LBB473
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL1089
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1072
	.4byte	0x4931
	.uleb128 0x24
	.4byte	.LVL1073
	.4byte	0x47cf
	.uleb128 0x24
	.4byte	.LVL1076
	.4byte	0x4af7
	.uleb128 0x24
	.4byte	.LVL1077
	.4byte	0x190b
	.uleb128 0x24
	.4byte	.LVL1078
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1082
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1083
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1084
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1085
	.4byte	0x1815
	.uleb128 0x24
	.4byte	.LVL1086
	.4byte	0x4a91
	.uleb128 0x24
	.4byte	.LVL1087
	.4byte	0x4af7
	.uleb128 0x24
	.4byte	.LVL1092
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1093
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL1094
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1095
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1096
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1098
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL1099
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1100
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL1101
	.4byte	0x4994
	.uleb128 0x24
	.4byte	.LVL1102
	.4byte	0x4994
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x70b
	.uleb128 0x3a
	.4byte	.LASF595
	.byte	0x4
	.2byte	0x116
	.4byte	0x64
	.4byte	.LFB170
	.4byte	.LFE170-.LFB170
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x482d
	.uleb128 0x50
	.ascii	"die\000"
	.byte	0x4
	.2byte	0x118
	.4byte	0x317
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x51
	.ascii	"blk\000"
	.byte	0x4
	.2byte	0x119
	.4byte	0x30c
	.4byte	.LLST82
	.uleb128 0x44
	.4byte	.LASF596
	.byte	0x4
	.2byte	0x11a
	.4byte	0x317
	.4byte	.LLST83
	.uleb128 0x3d
	.4byte	.LASF593
	.byte	0x4
	.2byte	0x11b
	.4byte	0x47c9
	.uleb128 0x24
	.4byte	.LVL361
	.4byte	0x1b05
	.byte	0
	.uleb128 0x22
	.4byte	.LASF597
	.byte	0x4
	.byte	0xac
	.4byte	0x64
	.4byte	.LFB169
	.4byte	.LFE169-.LFB169
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x48dd
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x4
	.byte	0xae
	.4byte	0x317
	.4byte	.LLST206
	.uleb128 0x49
	.ascii	"blk\000"
	.byte	0x4
	.byte	0xaf
	.4byte	0x30c
	.4byte	.LLST207
	.uleb128 0x2c
	.4byte	.LASF489
	.byte	0x4
	.byte	0xb0
	.4byte	0x32d
	.4byte	.LLST208
	.uleb128 0x34
	.4byte	.LASF593
	.byte	0x4
	.byte	0xb1
	.4byte	0x47c9
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x48ed
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6576
	.uleb128 0x24
	.4byte	.LVL1051
	.4byte	0x4931
	.uleb128 0x24
	.4byte	.LVL1053
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1054
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1055
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1056
	.4byte	0x447f
	.uleb128 0x24
	.4byte	.LVL1058
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1059
	.4byte	0x58be
	.uleb128 0x24
	.4byte	.LVL1065
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1069
	.4byte	0x177a
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x48ed
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xa
	.byte	0
	.uleb128 0x4
	.4byte	0x48dd
	.uleb128 0x22
	.4byte	.LASF598
	.byte	0x4
	.byte	0x9c
	.4byte	0x30c
	.4byte	.LFB168
	.4byte	.LFE168-.LFB168
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4931
	.uleb128 0x49
	.ascii	"blk\000"
	.byte	0x4
	.byte	0x9e
	.4byte	0x30c
	.4byte	.LLST19
	.uleb128 0x20
	.4byte	.LASF599
	.byte	0x4
	.byte	0x9f
	.4byte	0x30c
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x24
	.4byte	.LVL53
	.4byte	0x4af7
	.byte	0
	.uleb128 0x48
	.4byte	.LASF600
	.byte	0x4
	.byte	0x94
	.4byte	.LFB167
	.4byte	.LFE167-.LFB167
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4994
	.uleb128 0x5a
	.4byte	0x17d1
	.4byte	.LBB227
	.4byte	.Ldebug_ranges0+0x18
	.byte	0x4
	.byte	0x98
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x18
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x18
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.uleb128 0x24
	.4byte	.LVL50
	.4byte	0x671c
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x22
	.4byte	.LASF601
	.byte	0x4
	.byte	0x4f
	.4byte	0x64
	.4byte	.LFB166
	.4byte	.LFE166-.LFB166
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4a91
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x4
	.byte	0x51
	.4byte	0x64
	.4byte	.LLST173
	.uleb128 0x2c
	.4byte	.LASF602
	.byte	0x4
	.byte	0x52
	.4byte	0x64
	.4byte	.LLST174
	.uleb128 0x2c
	.4byte	.LASF593
	.byte	0x4
	.byte	0x53
	.4byte	0x47c9
	.4byte	.LLST175
	.uleb128 0x2c
	.4byte	.LASF505
	.byte	0x4
	.byte	0x54
	.4byte	0x30c
	.4byte	.LLST176
	.uleb128 0x46
	.4byte	.LASF603
	.byte	0x4
	.byte	0x67
	.uleb128 0x5b
	.4byte	.LASF363
	.4byte	0x1b00
	.uleb128 0x2d
	.4byte	0x17d1
	.4byte	.LBB420
	.4byte	.Ldebug_ranges0+0x158
	.byte	0x4
	.byte	0x5f
	.4byte	0x4a48
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x158
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x158
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL841
	.4byte	0x671c
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL837
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL842
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL843
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL844
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL845
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL848
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL849
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL851
	.4byte	0x177a
	.byte	0
	.uleb128 0x48
	.4byte	.LASF604
	.byte	0x4
	.byte	0x41
	.4byte	.LFB165
	.4byte	.LFE165-.LFB165
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4aef
	.uleb128 0x23
	.4byte	.LASF537
	.byte	0x4
	.byte	0x41
	.4byte	0x63b
	.4byte	.LLST111
	.uleb128 0x23
	.4byte	.LASF605
	.byte	0x4
	.byte	0x41
	.4byte	0x544
	.4byte	.LLST112
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x43
	.4byte	0x317
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x29d0
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6533
	.uleb128 0x24
	.4byte	.LVL522
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL523
	.4byte	0x6727
	.byte	0
	.uleb128 0x5c
	.4byte	.LASF606
	.byte	0x4
	.byte	0x26
	.byte	0x1
	.uleb128 0x2a
	.4byte	.LASF607
	.byte	0x4
	.byte	0x17
	.4byte	0x64
	.4byte	.LFB163
	.4byte	.LFE163-.LFB163
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4b52
	.uleb128 0x23
	.4byte	.LASF179
	.byte	0x4
	.byte	0x17
	.4byte	0x30c
	.4byte	.LLST16
	.uleb128 0x49
	.ascii	"die\000"
	.byte	0x4
	.byte	0x19
	.4byte	0x317
	.4byte	.LLST17
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x4
	.byte	0x1a
	.4byte	0x317
	.uleb128 0x34
	.4byte	.LASF605
	.byte	0x4
	.byte	0x1b
	.4byte	0x544
	.uleb128 0x2c
	.4byte	.LASF370
	.byte	0x4
	.byte	0x1c
	.4byte	0x30c
	.4byte	.LLST18
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF608
	.byte	0x4
	.byte	0x4
	.4byte	0x64
	.4byte	.LFB162
	.4byte	.LFE162-.LFB162
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4ba9
	.uleb128 0x23
	.4byte	.LASF179
	.byte	0x4
	.byte	0x4
	.4byte	0x30c
	.4byte	.LLST14
	.uleb128 0x49
	.ascii	"die\000"
	.byte	0x4
	.byte	0x6
	.4byte	0x317
	.4byte	.LLST15
	.uleb128 0x34
	.4byte	.LASF605
	.byte	0x4
	.byte	0x7
	.4byte	0x544
	.uleb128 0x34
	.4byte	.LASF370
	.byte	0x4
	.byte	0x8
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL41
	.4byte	0x6727
	.byte	0
	.uleb128 0x5d
	.4byte	.LASF670
	.byte	0x3
	.2byte	0x4bb
	.4byte	0x317
	.4byte	.LFB161
	.4byte	.LFE161-.LFB161
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x3a
	.4byte	.LASF609
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x64
	.4byte	.LFB160
	.4byte	.LFE160-.LFB160
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c13
	.uleb128 0x43
	.4byte	.LASF94
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x99
	.4byte	.LLST283
	.uleb128 0x43
	.4byte	.LASF115
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x99
	.4byte	.LLST284
	.uleb128 0x4d
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b6
	.4byte	0x4c13
	.4byte	.LLST285
	.uleb128 0x24
	.4byte	.LVL1560
	.4byte	0x4d3f
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x79
	.uleb128 0x3a
	.4byte	.LASF610
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x64
	.4byte	.LFB159
	.4byte	.LFE159-.LFB159
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4c6d
	.uleb128 0x43
	.4byte	.LASF94
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x99
	.4byte	.LLST269
	.uleb128 0x43
	.4byte	.LASF115
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x99
	.4byte	.LLST270
	.uleb128 0x4d
	.ascii	"buf\000"
	.byte	0x3
	.2byte	0x4b0
	.4byte	0x4c13
	.4byte	.LLST271
	.uleb128 0x24
	.4byte	.LVL1458
	.4byte	0x50ca
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF611
	.byte	0x3
	.2byte	0x481
	.4byte	0x64
	.4byte	.LFB158
	.4byte	.LFE158-.LFB158
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4d3f
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x481
	.4byte	0x317
	.4byte	.LLST234
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x481
	.4byte	0x317
	.4byte	.LLST235
	.uleb128 0x44
	.4byte	.LASF518
	.byte	0x3
	.2byte	0x483
	.4byte	0x30c
	.4byte	.LLST236
	.uleb128 0x3e
	.ascii	"tmp\000"
	.byte	0x3
	.2byte	0x484
	.4byte	0x30c
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x485
	.4byte	0x317
	.4byte	.LLST237
	.uleb128 0x44
	.4byte	.LASF447
	.byte	0x3
	.2byte	0x486
	.4byte	0x317
	.4byte	.LLST238
	.uleb128 0x44
	.4byte	.LASF424
	.byte	0x3
	.2byte	0x486
	.4byte	0x317
	.4byte	.LLST239
	.uleb128 0x3d
	.4byte	.LASF413
	.byte	0x3
	.2byte	0x487
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL1226
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1232
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1233
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1234
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1235
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1238
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1239
	.4byte	0x233b
	.byte	0
	.uleb128 0x4c
	.4byte	.LASF612
	.byte	0x3
	.2byte	0x3be
	.4byte	0x64
	.4byte	.LFB157
	.4byte	.LFE157-.LFB157
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f6d
	.uleb128 0x4d
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x3be
	.4byte	0x301
	.4byte	.LLST272
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x3be
	.4byte	0x317
	.4byte	.LLST273
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x3be
	.4byte	0x317
	.4byte	.LLST274
	.uleb128 0x43
	.4byte	.LASF517
	.byte	0x3
	.2byte	0x3be
	.4byte	0x384f
	.4byte	.LLST275
	.uleb128 0x3d
	.4byte	.LASF381
	.byte	0x3
	.2byte	0x3c0
	.4byte	0x1d4c
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x317
	.4byte	.LLST276
	.uleb128 0x44
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x317
	.4byte	.LLST277
	.uleb128 0x44
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x3c1
	.4byte	0x317
	.4byte	.LLST278
	.uleb128 0x44
	.4byte	.LASF447
	.byte	0x3
	.2byte	0x3c2
	.4byte	0x317
	.4byte	.LLST279
	.uleb128 0x3e
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x3c2
	.4byte	0x317
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x317
	.4byte	.LLST280
	.uleb128 0x44
	.4byte	.LASF615
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x317
	.4byte	.LLST281
	.uleb128 0x3d
	.4byte	.LASF378
	.byte	0x3
	.2byte	0x3c3
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF616
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x317
	.uleb128 0x3d
	.4byte	.LASF617
	.byte	0x3
	.2byte	0x3c4
	.4byte	0x317
	.uleb128 0x44
	.4byte	.LASF423
	.byte	0x3
	.2byte	0x3c5
	.4byte	0x12cb
	.4byte	.LLST282
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x39f2
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6469
	.uleb128 0x52
	.4byte	.LBB516
	.4byte	.LBE516-.LBB516
	.4byte	0x4e8b
	.uleb128 0x1f
	.4byte	.LASF386
	.byte	0x3
	.2byte	0x43a
	.4byte	0x338
	.uleb128 0x3
	.byte	0x91
	.sleb128 -68
	.uleb128 0x24
	.4byte	.LVL1538
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1539
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1540
	.4byte	0x6727
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1462
	.4byte	0x3855
	.uleb128 0x24
	.4byte	.LVL1473
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1479
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1480
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1481
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1482
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1485
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1488
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1495
	.4byte	0x1c7f
	.uleb128 0x24
	.4byte	.LVL1497
	.4byte	0x3fa5
	.uleb128 0x24
	.4byte	.LVL1498
	.4byte	0x1e7a
	.uleb128 0x24
	.4byte	.LVL1499
	.4byte	0x1c7f
	.uleb128 0x24
	.4byte	.LVL1500
	.4byte	0x1c7f
	.uleb128 0x24
	.4byte	.LVL1505
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1510
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1512
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1515
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1516
	.4byte	0x23f1
	.uleb128 0x24
	.4byte	.LVL1520
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1531
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1541
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1544
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1547
	.4byte	0x177a
	.uleb128 0x24
	.4byte	.LVL1551
	.4byte	0x4f91
	.uleb128 0x24
	.4byte	.LVL1555
	.4byte	0x6727
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF618
	.byte	0x3
	.2byte	0x3b5
	.4byte	0x64
	.4byte	.LFB156
	.4byte	.LFE156-.LFB156
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x4f91
	.uleb128 0x24
	.4byte	.LVL1216
	.4byte	0x4f91
	.byte	0
	.uleb128 0x4f
	.4byte	.LASF619
	.byte	0x3
	.2byte	0x385
	.4byte	.LFB155
	.4byte	.LFE155-.LFB155
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x50b5
	.uleb128 0x4d
	.ascii	"req\000"
	.byte	0x3
	.2byte	0x385
	.4byte	0xf4b
	.4byte	.LLST227
	.uleb128 0x43
	.4byte	.LASF378
	.byte	0x3
	.2byte	0x385
	.4byte	0x317
	.4byte	.LLST228
	.uleb128 0x43
	.4byte	.LASF620
	.byte	0x3
	.2byte	0x385
	.4byte	0x317
	.4byte	.LLST229
	.uleb128 0x43
	.4byte	.LASF423
	.byte	0x3
	.2byte	0x385
	.4byte	0x12cb
	.4byte	.LLST230
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x387
	.4byte	0x317
	.4byte	.LLST231
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x388
	.4byte	0x317
	.4byte	.LLST232
	.uleb128 0x44
	.4byte	.LASF447
	.byte	0x3
	.2byte	0x388
	.4byte	0x317
	.4byte	.LLST233
	.uleb128 0x3d
	.4byte	.LASF406
	.byte	0x3
	.2byte	0x389
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF381
	.byte	0x3
	.2byte	0x38a
	.4byte	0x1d4c
	.uleb128 0x3d
	.4byte	.LASF471
	.byte	0x3
	.2byte	0x38b
	.4byte	0x317
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x50c5
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6441
	.uleb128 0x24
	.4byte	.LVL1195
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1197
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1198
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1199
	.4byte	0x1e1e
	.uleb128 0x24
	.4byte	.LVL1200
	.4byte	0x23f1
	.uleb128 0x24
	.4byte	.LVL1201
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL1203
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1205
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1207
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1210
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1211
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1214
	.4byte	0x6727
	.byte	0
	.uleb128 0xc
	.4byte	0x3c
	.4byte	0x50c5
	.uleb128 0x17
	.4byte	0x2e
	.byte	0xc
	.byte	0
	.uleb128 0x4
	.4byte	0x50b5
	.uleb128 0x4c
	.4byte	.LASF621
	.byte	0x3
	.2byte	0x32c
	.4byte	0x64
	.4byte	.LFB154
	.4byte	.LFE154-.LFB154
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52a7
	.uleb128 0x4d
	.ascii	"LUN\000"
	.byte	0x3
	.2byte	0x32c
	.4byte	0x301
	.4byte	.LLST254
	.uleb128 0x43
	.4byte	.LASF515
	.byte	0x3
	.2byte	0x32c
	.4byte	0x317
	.4byte	.LLST255
	.uleb128 0x43
	.4byte	.LASF516
	.byte	0x3
	.2byte	0x32c
	.4byte	0x317
	.4byte	.LLST256
	.uleb128 0x43
	.4byte	.LASF517
	.byte	0x3
	.2byte	0x32c
	.4byte	0x384f
	.4byte	.LLST257
	.uleb128 0x3e
	.ascii	"ret\000"
	.byte	0x3
	.2byte	0x32e
	.4byte	0x64
	.uleb128 0x44
	.4byte	.LASF613
	.byte	0x3
	.2byte	0x32f
	.4byte	0x317
	.4byte	.LLST258
	.uleb128 0x44
	.4byte	.LASF614
	.byte	0x3
	.2byte	0x32f
	.4byte	0x317
	.4byte	.LLST259
	.uleb128 0x44
	.4byte	.LASF622
	.byte	0x3
	.2byte	0x32f
	.4byte	0x317
	.4byte	.LLST260
	.uleb128 0x51
	.ascii	"lpa\000"
	.byte	0x3
	.2byte	0x330
	.4byte	0x317
	.4byte	.LLST261
	.uleb128 0x51
	.ascii	"ppa\000"
	.byte	0x3
	.2byte	0x330
	.4byte	0x317
	.4byte	.LLST262
	.uleb128 0x51
	.ascii	"n\000"
	.byte	0x3
	.2byte	0x332
	.4byte	0x317
	.4byte	.LLST263
	.uleb128 0x44
	.4byte	.LASF378
	.byte	0x3
	.2byte	0x332
	.4byte	0x317
	.4byte	.LLST264
	.uleb128 0x44
	.4byte	.LASF615
	.byte	0x3
	.2byte	0x332
	.4byte	0x317
	.4byte	.LLST265
	.uleb128 0x44
	.4byte	.LASF623
	.byte	0x3
	.2byte	0x333
	.4byte	0x317
	.4byte	.LLST266
	.uleb128 0x3d
	.4byte	.LASF624
	.byte	0x3
	.2byte	0x333
	.4byte	0x317
	.uleb128 0x44
	.4byte	.LASF625
	.byte	0x3
	.2byte	0x334
	.4byte	0x317
	.4byte	.LLST267
	.uleb128 0x44
	.4byte	.LASF626
	.byte	0x3
	.2byte	0x335
	.4byte	0x317
	.4byte	.LLST268
	.uleb128 0x52
	.4byte	.LBB515
	.4byte	.LBE515-.LBB515
	.4byte	0x5216
	.uleb128 0x3d
	.4byte	.LASF393
	.byte	0x3
	.2byte	0x379
	.4byte	0x30c
	.uleb128 0x24
	.4byte	.LVL1443
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL1444
	.4byte	0x1e7a
	.byte	0
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB512
	.4byte	.LBE512-.LBB512
	.byte	0x3
	.2byte	0x34f
	.4byte	0x5270
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB513
	.4byte	.LBE513-.LBB513
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB514
	.4byte	.LBE514-.LBB514
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL1428
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1390
	.4byte	0x3781
	.uleb128 0x24
	.4byte	.LVL1396
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1411
	.4byte	0x1c7f
	.uleb128 0x24
	.4byte	.LVL1414
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1434
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1440
	.4byte	0x177a
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF627
	.byte	0x3
	.2byte	0x325
	.4byte	0x64
	.4byte	.LFB153
	.4byte	.LFE153-.LFB153
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x52cb
	.uleb128 0x24
	.4byte	.LVL1221
	.4byte	0x52cb
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF628
	.byte	0x3
	.2byte	0x31c
	.4byte	0x64
	.4byte	.LFB152
	.4byte	.LFE152-.LFB152
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x530a
	.uleb128 0x24
	.4byte	.LVL1217
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1218
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1219
	.4byte	0x3734
	.uleb128 0x24
	.4byte	.LVL1220
	.4byte	0x3593
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF629
	.byte	0x3
	.2byte	0x2e6
	.4byte	0x64
	.4byte	.LFB151
	.4byte	.LFE151-.LFB151
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5399
	.uleb128 0x3d
	.4byte	.LASF630
	.byte	0x3
	.2byte	0x2e8
	.4byte	0x5399
	.uleb128 0x3f
	.4byte	.LASF631
	.byte	0x3
	.2byte	0x2f3
	.uleb128 0x52
	.4byte	.LBB524
	.4byte	.LBE524-.LBB524
	.4byte	0x5359
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x310
	.4byte	0x317
	.uleb128 0x24
	.4byte	.LVL1644
	.4byte	0x1c7f
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1637
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1638
	.4byte	0x586d
	.uleb128 0x24
	.4byte	.LVL1639
	.4byte	0x56e5
	.uleb128 0x24
	.4byte	.LVL1640
	.4byte	0x55e9
	.uleb128 0x24
	.4byte	.LVL1641
	.4byte	0x4400
	.uleb128 0x24
	.4byte	.LVL1642
	.4byte	0x482d
	.uleb128 0x24
	.4byte	.LVL1643
	.4byte	0x2812
	.byte	0
	.uleb128 0x8
	.byte	0x4
	.4byte	0x424
	.uleb128 0x4c
	.4byte	.LASF632
	.byte	0x3
	.2byte	0x26e
	.4byte	0x64
	.4byte	.LFB150
	.4byte	.LFE150-.LFB150
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5498
	.uleb128 0x3d
	.4byte	.LASF501
	.byte	0x3
	.2byte	0x270
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF633
	.byte	0x3
	.2byte	0x270
	.4byte	0x30c
	.4byte	.LLST288
	.uleb128 0x44
	.4byte	.LASF634
	.byte	0x3
	.2byte	0x271
	.4byte	0x30c
	.4byte	.LLST289
	.uleb128 0x3e
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x272
	.4byte	0x30c
	.uleb128 0x47
	.4byte	.LASF635
	.byte	0x3
	.2byte	0x2bc
	.4byte	.L1750
	.uleb128 0x47
	.4byte	.LASF636
	.byte	0x3
	.2byte	0x2cc
	.4byte	.L1752
	.uleb128 0x24
	.4byte	.LVL1606
	.4byte	0x4400
	.uleb128 0x24
	.4byte	.LVL1607
	.4byte	0x482d
	.uleb128 0x24
	.4byte	.LVL1608
	.4byte	0x4605
	.uleb128 0x24
	.4byte	.LVL1615
	.4byte	0x4994
	.uleb128 0x24
	.4byte	.LVL1616
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1617
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL1618
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1620
	.4byte	0x5498
	.uleb128 0x24
	.4byte	.LVL1624
	.4byte	0x58be
	.uleb128 0x24
	.4byte	.LVL1625
	.4byte	0x4400
	.uleb128 0x24
	.4byte	.LVL1627
	.4byte	0x5498
	.uleb128 0x24
	.4byte	.LVL1629
	.4byte	0x5498
	.uleb128 0x24
	.4byte	.LVL1633
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1634
	.4byte	0x424d
	.uleb128 0x24
	.4byte	.LVL1635
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1636
	.4byte	0x2812
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF637
	.byte	0x3
	.2byte	0x20f
	.4byte	0x64
	.4byte	.LFB149
	.4byte	.LFE149-.LFB149
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x55bf
	.uleb128 0x43
	.4byte	.LASF501
	.byte	0x3
	.2byte	0x20f
	.4byte	0x30c
	.4byte	.LLST96
	.uleb128 0x43
	.4byte	.LASF638
	.byte	0x3
	.2byte	0x20f
	.4byte	0x301
	.4byte	.LLST97
	.uleb128 0x44
	.4byte	.LASF633
	.byte	0x3
	.2byte	0x211
	.4byte	0x30c
	.4byte	.LLST98
	.uleb128 0x3d
	.4byte	.LASF358
	.byte	0x3
	.2byte	0x211
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF377
	.byte	0x3
	.2byte	0x212
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF179
	.byte	0x3
	.2byte	0x212
	.4byte	0x30c
	.uleb128 0x44
	.4byte	.LASF639
	.byte	0x3
	.2byte	0x213
	.4byte	0x30c
	.4byte	.LLST99
	.uleb128 0x44
	.4byte	.LASF640
	.byte	0x3
	.2byte	0x214
	.4byte	0x30c
	.4byte	.LLST100
	.uleb128 0x44
	.4byte	.LASF641
	.byte	0x3
	.2byte	0x215
	.4byte	0x30c
	.4byte	.LLST101
	.uleb128 0x44
	.4byte	.LASF642
	.byte	0x3
	.2byte	0x216
	.4byte	0x30c
	.4byte	.LLST102
	.uleb128 0x47
	.4byte	.LASF643
	.byte	0x3
	.2byte	0x240
	.4byte	.L488
	.uleb128 0x24
	.4byte	.LVL413
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL423
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL434
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL436
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL437
	.4byte	0x55bf
	.uleb128 0x24
	.4byte	.LVL438
	.4byte	0x4af7
	.uleb128 0x24
	.4byte	.LVL443
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL446
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL447
	.4byte	0x55bf
	.uleb128 0x24
	.4byte	.LVL448
	.4byte	0x4af7
	.uleb128 0x24
	.4byte	.LVL453
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL456
	.4byte	0x4367
	.byte	0
	.uleb128 0x3b
	.4byte	.LASF644
	.byte	0x3
	.2byte	0x201
	.4byte	0x64
	.byte	0x1
	.4byte	0x55e9
	.uleb128 0x3c
	.4byte	.LASF179
	.byte	0x3
	.2byte	0x201
	.4byte	0x30c
	.uleb128 0x3d
	.4byte	.LASF645
	.byte	0x3
	.2byte	0x203
	.4byte	0x30c
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF646
	.byte	0x3
	.2byte	0x1ec
	.4byte	0x64
	.4byte	.LFB147
	.4byte	.LFE147-.LFB147
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x56e5
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB409
	.4byte	.LBE409-.LBB409
	.byte	0x3
	.2byte	0x1f9
	.4byte	0x565d
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB410
	.4byte	.LBE410-.LBB410
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB411
	.4byte	.LBE411-.LBB411
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL771
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x53
	.4byte	0x17d1
	.4byte	.LBB412
	.4byte	.LBE412-.LBB412
	.byte	0x3
	.2byte	0x1fa
	.4byte	0x56b7
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB413
	.4byte	.LBE413-.LBB413
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB414
	.4byte	.LBE414-.LBB414
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL772
	.4byte	0x6732
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL768
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL769
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL770
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL773
	.4byte	0x231c
	.uleb128 0x24
	.4byte	.LVL774
	.4byte	0x31c5
	.byte	0
	.uleb128 0x3a
	.4byte	.LASF647
	.byte	0x3
	.2byte	0x116
	.4byte	0x64
	.4byte	.LFB146
	.4byte	.LFE146-.LFB146
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x586d
	.uleb128 0x51
	.ascii	"i\000"
	.byte	0x3
	.2byte	0x118
	.4byte	0x317
	.4byte	.LLST110
	.uleb128 0x3d
	.4byte	.LASF648
	.byte	0x3
	.2byte	0x119
	.4byte	0x317
	.uleb128 0x37
	.4byte	.LASF363
	.4byte	0x48ed
	.uleb128 0x5
	.byte	0x3
	.4byte	__func__.6317
	.uleb128 0x24
	.4byte	.LVL479
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL480
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL481
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL482
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL483
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL484
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL485
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL486
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL487
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL488
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL489
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL490
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL491
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL492
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL493
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL494
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL495
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL496
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL497
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL498
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL499
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL500
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL501
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL502
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL503
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL504
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL505
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL506
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL507
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL508
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL509
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL510
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL511
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL512
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL513
	.4byte	0x6748
	.uleb128 0x24
	.4byte	.LVL516
	.4byte	0x6727
	.byte	0
	.uleb128 0x2a
	.4byte	.LASF649
	.byte	0x3
	.byte	0xb6
	.4byte	0x64
	.4byte	.LFB145
	.4byte	.LFE145-.LFB145
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x58be
	.uleb128 0x23
	.4byte	.LASF630
	.byte	0x3
	.byte	0xb6
	.4byte	0x5399
	.4byte	.LLST11
	.uleb128 0x49
	.ascii	"i\000"
	.byte	0x3
	.byte	0xb8
	.4byte	0x317
	.4byte	.LLST12
	.uleb128 0x24
	.4byte	.LVL27
	.4byte	0x58ff
	.uleb128 0x24
	.4byte	.LVL28
	.4byte	0x58ff
	.uleb128 0x24
	.4byte	.LVL32
	.4byte	0x58be
	.byte	0
	.uleb128 0x22
	.4byte	.LASF650
	.byte	0x3
	.byte	0xa4
	.4byte	0x64
	.4byte	.LFB144
	.4byte	.LFE144-.LFB144
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x58e7
	.uleb128 0x23
	.4byte	.LASF651
	.byte	0x3
	.byte	0xa4
	.4byte	0x30c
	.4byte	.LLST10
	.byte	0
	.uleb128 0x57
	.4byte	.LASF652
	.byte	0x3
	.byte	0x9e
	.byte	0x1
	.4byte	0x58ff
	.uleb128 0x33
	.4byte	.LASF653
	.byte	0x3
	.byte	0x9e
	.4byte	0x317
	.byte	0
	.uleb128 0x22
	.4byte	.LASF654
	.byte	0x3
	.byte	0x7f
	.4byte	0x30c
	.4byte	.LFB142
	.4byte	.LFE142-.LFB142
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x593e
	.uleb128 0x2b
	.ascii	"num\000"
	.byte	0x3
	.byte	0x7f
	.4byte	0x317
	.4byte	.LLST9
	.uleb128 0x29
	.ascii	"i\000"
	.byte	0x3
	.byte	0x81
	.4byte	0x30c
	.uleb128 0x20
	.4byte	.LASF655
	.byte	0x3
	.byte	0x82
	.4byte	0x317
	.uleb128 0x1
	.byte	0x52
	.byte	0
	.uleb128 0x22
	.4byte	.LASF656
	.byte	0x3
	.byte	0x71
	.4byte	0x64
	.4byte	.LFB141
	.4byte	.LFE141-.LFB141
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5984
	.uleb128 0x23
	.4byte	.LASF86
	.byte	0x3
	.byte	0x71
	.4byte	0xb6
	.4byte	.LLST63
	.uleb128 0x29
	.ascii	"buf\000"
	.byte	0x3
	.byte	0x73
	.4byte	0xb6
	.uleb128 0x24
	.4byte	.LVL282
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL283
	.4byte	0x5984
	.byte	0
	.uleb128 0x22
	.4byte	.LASF657
	.byte	0x3
	.byte	0xa
	.4byte	0x64
	.4byte	.LFB140
	.4byte	.LFE140-.LFB140
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c7a
	.uleb128 0x23
	.4byte	.LASF86
	.byte	0x3
	.byte	0xa
	.4byte	0xb6
	.4byte	.LLST60
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
	.uleb128 0x52
	.4byte	.LBB282
	.4byte	.LBE282-.LBB282
	.4byte	0x59ef
	.uleb128 0x2c
	.4byte	.LASF393
	.byte	0x3
	.byte	0x57
	.4byte	0x30c
	.4byte	.LLST61
	.uleb128 0x24
	.4byte	.LVL273
	.4byte	0x3fa5
	.uleb128 0x24
	.4byte	.LVL275
	.4byte	0x6753
	.byte	0
	.uleb128 0x52
	.4byte	.LBB283
	.4byte	.LBE283-.LBB283
	.4byte	0x5a15
	.uleb128 0x2c
	.4byte	.LASF393
	.byte	0x3
	.byte	0x60
	.4byte	0x30c
	.4byte	.LLST62
	.uleb128 0x24
	.4byte	.LVL279
	.4byte	0x6753
	.byte	0
	.uleb128 0x24
	.4byte	.LVL203
	.4byte	0x675f
	.uleb128 0x24
	.4byte	.LVL204
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL205
	.4byte	0x675f
	.uleb128 0x24
	.4byte	.LVL206
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL207
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL208
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL209
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL210
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL211
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL212
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL213
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL214
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL215
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL216
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL217
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL218
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL219
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL220
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL221
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL222
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL223
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL224
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL225
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL226
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL227
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL228
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL229
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL230
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL231
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL232
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL233
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL234
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL235
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL236
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL237
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL238
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL239
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL240
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL241
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL242
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL243
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL244
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL245
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL246
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL247
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL248
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL249
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL250
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL251
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL252
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL253
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL254
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL255
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL256
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL257
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL258
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL259
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL260
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL261
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL262
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL263
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL264
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL265
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL266
	.4byte	0x2788
	.uleb128 0x24
	.4byte	.LVL267
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL268
	.4byte	0x2739
	.uleb128 0x24
	.4byte	.LVL269
	.4byte	0x6753
	.uleb128 0x24
	.4byte	.LVL272
	.4byte	0x6753
	.byte	0
	.uleb128 0x5e
	.4byte	0x3414
	.4byte	.LFB276
	.4byte	.LFE276-.LFB276
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5c9c
	.uleb128 0x5f
	.4byte	0x3421
	.4byte	.LLST8
	.uleb128 0x2e
	.4byte	0x342d
	.byte	0
	.uleb128 0x5e
	.4byte	0x58e7
	.4byte	.LFB143
	.4byte	.LFE143-.LFB143
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5cb7
	.uleb128 0x60
	.4byte	0x58f3
	.uleb128 0x1
	.byte	0x50
	.byte	0
	.uleb128 0x5e
	.4byte	0x55bf
	.4byte	.LFB148
	.4byte	.LFE148-.LFB148
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5cef
	.uleb128 0x5f
	.4byte	0x55d0
	.4byte	.LLST13
	.uleb128 0x30
	.4byte	0x55dc
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x2e
	.4byte	0x55d0
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0
	.uleb128 0x30
	.4byte	0x5cd3
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x61
	.4byte	0x4aef
	.4byte	.LFB164
	.4byte	.LFE164-.LFB164
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x61
	.4byte	0x43f4
	.4byte	.LFB178
	.4byte	.LFE178-.LFB178
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x61
	.4byte	0x43e8
	.4byte	.LFB179
	.4byte	.LFE179-.LFB179
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x5e
	.4byte	0x4203
	.4byte	.LFB185
	.4byte	.LFE185-.LFB185
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5d72
	.uleb128 0x5f
	.4byte	0x420f
	.4byte	.LLST36
	.uleb128 0x30
	.4byte	0x421a
	.uleb128 0x52
	.4byte	.LBB258
	.4byte	.LBE258-.LBB258
	.4byte	0x5d68
	.uleb128 0x2e
	.4byte	0x420f
	.uleb128 0x32
	.4byte	.LBB259
	.4byte	.LBE259-.LBB259
	.uleb128 0x30
	.4byte	0x5d38
	.uleb128 0x24
	.4byte	.LVL115
	.4byte	0x6727
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL114
	.4byte	0x4177
	.byte	0
	.uleb128 0x5e
	.4byte	0x4228
	.4byte	.LFB184
	.4byte	.LFE184-.LFB184
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5dc8
	.uleb128 0x5f
	.4byte	0x4234
	.4byte	.LLST39
	.uleb128 0x30
	.4byte	0x423f
	.uleb128 0x52
	.4byte	.LBB262
	.4byte	.LBE262-.LBB262
	.4byte	0x5dbe
	.uleb128 0x2e
	.4byte	0x4234
	.uleb128 0x32
	.4byte	.LBB263
	.4byte	.LBE263-.LBB263
	.uleb128 0x30
	.4byte	0x5d8e
	.uleb128 0x24
	.4byte	.LVL127
	.4byte	0x6727
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL126
	.4byte	0x4108
	.byte	0
	.uleb128 0x5e
	.4byte	0x3f26
	.4byte	.LFB192
	.4byte	.LFE192-.LFB192
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5e7a
	.uleb128 0x5f
	.4byte	0x3f37
	.4byte	.LLST48
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
	.4byte	.LBB266
	.4byte	.LBE266-.LBB266
	.uleb128 0x2e
	.4byte	0x3f37
	.uleb128 0x32
	.4byte	.LBB267
	.4byte	.LBE267-.LBB267
	.uleb128 0x30
	.4byte	0x5de4
	.uleb128 0x30
	.4byte	0x5de9
	.uleb128 0x62
	.4byte	0x5dee
	.uleb128 0x1
	.byte	0x56
	.uleb128 0x30
	.4byte	0x5df3
	.uleb128 0x63
	.4byte	0x5df8
	.4byte	.LLST49
	.uleb128 0x62
	.4byte	0x5dfd
	.uleb128 0x1
	.byte	0x57
	.uleb128 0x30
	.4byte	0x5e02
	.uleb128 0x30
	.4byte	0x5e07
	.uleb128 0x24
	.4byte	.LVL160
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL162
	.4byte	0x4086
	.uleb128 0x24
	.4byte	.LVL163
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL164
	.4byte	0x4203
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x3d76
	.4byte	.LFB195
	.4byte	.LFE195-.LFB195
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5ee6
	.uleb128 0x5f
	.4byte	0x3d87
	.4byte	.LLST50
	.uleb128 0x62
	.4byte	0x3d93
	.uleb128 0x1
	.byte	0x55
	.uleb128 0x30
	.4byte	0x3d9f
	.uleb128 0x30
	.4byte	0x3dab
	.uleb128 0x52
	.4byte	.LBB270
	.4byte	.LBE270-.LBB270
	.4byte	0x5edc
	.uleb128 0x2e
	.4byte	0x3d87
	.uleb128 0x32
	.4byte	.LBB271
	.4byte	.LBE271-.LBB271
	.uleb128 0x30
	.4byte	0x5e96
	.uleb128 0x30
	.4byte	0x5e9d
	.uleb128 0x30
	.4byte	0x5ea2
	.uleb128 0x24
	.4byte	.LVL167
	.4byte	0x6727
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL168
	.4byte	0x424d
	.byte	0
	.uleb128 0x5e
	.4byte	0x3710
	.4byte	.LFB206
	.4byte	.LFE206-.LFB206
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f1e
	.uleb128 0x5f
	.4byte	0x371d
	.4byte	.LLST51
	.uleb128 0x30
	.4byte	0x3729
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x70
	.uleb128 0x2e
	.4byte	0x371d
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x70
	.uleb128 0x30
	.4byte	0x5f02
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x3414
	.4byte	.LFB212
	.4byte	.LFE212-.LFB212
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f4d
	.uleb128 0x5f
	.4byte	0x3421
	.4byte	.LLST52
	.uleb128 0x5f
	.4byte	0x342d
	.4byte	.LLST53
	.uleb128 0x24
	.4byte	.LVL173
	.4byte	0x5c7a
	.byte	0
	.uleb128 0x5e
	.4byte	0x2d0e
	.4byte	.LFB222
	.4byte	.LFE222-.LFB222
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5f8d
	.uleb128 0x60
	.4byte	0x2d1b
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x2d27
	.uleb128 0x32
	.4byte	.LBB280
	.4byte	.LBE280-.LBB280
	.uleb128 0x2e
	.4byte	0x2d1b
	.uleb128 0x32
	.4byte	.LBB281
	.4byte	.LBE281-.LBB281
	.uleb128 0x62
	.4byte	0x5f67
	.uleb128 0x1
	.byte	0x51
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x2681
	.4byte	.LFB237
	.4byte	.LFE237-.LFB237
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fc2
	.uleb128 0x5f
	.4byte	0x2692
	.4byte	.LLST68
	.uleb128 0x32
	.4byte	.LBB286
	.4byte	.LBE286-.LBB286
	.uleb128 0x2e
	.4byte	0x2692
	.uleb128 0x24
	.4byte	.LVL304
	.4byte	0x4228
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x1e11
	.4byte	.LFB256
	.4byte	.LFE256-.LFB256
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x5fe8
	.uleb128 0x24
	.4byte	.LVL338
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL339
	.4byte	0x1e7a
	.byte	0
	.uleb128 0x5e
	.4byte	0x190b
	.4byte	.LFB265
	.4byte	.LFE265-.LFB265
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x60fd
	.uleb128 0x5f
	.4byte	0x191b
	.4byte	.LLST103
	.uleb128 0x30
	.4byte	0x1926
	.uleb128 0x30
	.4byte	0x1931
	.uleb128 0x30
	.4byte	0x193c
	.uleb128 0x32
	.4byte	.LBB313
	.4byte	.LBE313-.LBB313
	.uleb128 0x2e
	.4byte	0x191b
	.uleb128 0x32
	.4byte	.LBB314
	.4byte	.LBE314-.LBB314
	.uleb128 0x30
	.4byte	0x6004
	.uleb128 0x62
	.4byte	0x6009
	.uleb128 0x3
	.byte	0x91
	.sleb128 -88
	.uleb128 0x62
	.4byte	0x600e
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x64
	.4byte	0x1947
	.uleb128 0x2d
	.4byte	0x17d1
	.4byte	.LBB315
	.4byte	.Ldebug_ranges0+0xa8
	.byte	0x1
	.byte	0xa7
	.4byte	0x6097
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xa8
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xa8
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.uleb128 0x24
	.4byte	.LVL461
	.4byte	0x671c
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x2d
	.4byte	0x17d1
	.4byte	.LBB321
	.4byte	.Ldebug_ranges0+0xc0
	.byte	0x1
	.byte	0xa8
	.4byte	0x60df
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xc0
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0xc0
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL462
	.4byte	0x195f
	.uleb128 0x24
	.4byte	.LVL463
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL464
	.4byte	0x195f
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x17d1
	.4byte	.LFB267
	.4byte	.LFE267-.LFB267
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6176
	.uleb128 0x5f
	.4byte	0x17e1
	.4byte	.LLST107
	.uleb128 0x5f
	.4byte	0x17ea
	.4byte	.LLST108
	.uleb128 0x5f
	.4byte	0x17f3
	.4byte	.LLST109
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0xf8
	.4byte	0x613f
	.uleb128 0x30
	.4byte	0x17fd
	.uleb128 0x30
	.4byte	0x1808
	.byte	0
	.uleb128 0x32
	.4byte	.LBB355
	.4byte	.LBE355-.LBB355
	.uleb128 0x2e
	.4byte	0x17f3
	.uleb128 0x2e
	.4byte	0x17ea
	.uleb128 0x2e
	.4byte	0x17e1
	.uleb128 0x32
	.4byte	.LBB356
	.4byte	.LBE356-.LBB356
	.uleb128 0x30
	.4byte	0x6134
	.uleb128 0x30
	.4byte	0x6139
	.uleb128 0x24
	.4byte	.LVL478
	.4byte	0x671c
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x1d67
	.4byte	.LFB258
	.4byte	.LFE258-.LFB258
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6210
	.uleb128 0x62
	.4byte	0x1d78
	.uleb128 0x1
	.byte	0x50
	.uleb128 0x30
	.4byte	0x1d84
	.uleb128 0x30
	.4byte	0x1d8e
	.uleb128 0x30
	.4byte	0x1d9a
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x140
	.4byte	0x6200
	.uleb128 0x30
	.4byte	0x6189
	.uleb128 0x30
	.4byte	0x6190
	.uleb128 0x63
	.4byte	0x6195
	.4byte	.LLST141
	.uleb128 0x62
	.4byte	0x619a
	.uleb128 0x3
	.byte	0x91
	.sleb128 -88
	.uleb128 0x52
	.4byte	.LBB377
	.4byte	.LBE377-.LBB377
	.4byte	0x61f6
	.uleb128 0x62
	.4byte	0x1dab
	.uleb128 0x3
	.byte	0x91
	.sleb128 -108
	.uleb128 0x24
	.4byte	.LVL642
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL643
	.4byte	0x455a
	.uleb128 0x24
	.4byte	.LVL644
	.4byte	0x1e7a
	.byte	0
	.uleb128 0x24
	.4byte	.LVL641
	.4byte	0x392c
	.byte	0
	.uleb128 0x32
	.4byte	.LBB380
	.4byte	.LBE380-.LBB380
	.uleb128 0x30
	.4byte	0x1db9
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x1dc7
	.4byte	.LFB257
	.4byte	.LFE257-.LFB257
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x629b
	.uleb128 0x5f
	.4byte	0x1dd8
	.4byte	.LLST182
	.uleb128 0x30
	.4byte	0x1de4
	.uleb128 0x30
	.4byte	0x1df0
	.uleb128 0x30
	.4byte	0x1dfa
	.uleb128 0x30
	.4byte	0x1e04
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x190
	.uleb128 0x2e
	.4byte	0x1dd8
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x190
	.uleb128 0x30
	.4byte	0x622c
	.uleb128 0x62
	.4byte	0x6231
	.uleb128 0x1
	.byte	0x54
	.uleb128 0x63
	.4byte	0x6236
	.4byte	.LLST183
	.uleb128 0x62
	.4byte	0x623b
	.uleb128 0x1
	.byte	0x59
	.uleb128 0x24
	.4byte	.LVL894
	.4byte	0x45c2
	.uleb128 0x24
	.4byte	.LVL896
	.4byte	0x1e11
	.uleb128 0x24
	.4byte	.LVL897
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL898
	.4byte	0x4b52
	.uleb128 0x24
	.4byte	.LVL899
	.4byte	0x4994
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x23b1
	.4byte	.LFB241
	.4byte	.LFE241-.LFB241
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6315
	.uleb128 0x5f
	.4byte	0x23c2
	.4byte	.LLST184
	.uleb128 0x30
	.4byte	0x23ce
	.uleb128 0x52
	.4byte	.LBB441
	.4byte	.LBE441-.LBB441
	.4byte	0x630b
	.uleb128 0x2e
	.4byte	0x23c2
	.uleb128 0x32
	.4byte	.LBB442
	.4byte	.LBE442-.LBB442
	.uleb128 0x30
	.4byte	0x62b7
	.uleb128 0x24
	.4byte	.LVL903
	.4byte	0x4086
	.uleb128 0x24
	.4byte	.LVL904
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL905
	.4byte	0x2681
	.uleb128 0x24
	.4byte	.LVL906
	.4byte	0x1dc7
	.uleb128 0x24
	.4byte	.LVL907
	.4byte	0x6727
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL909
	.4byte	0x3f26
	.byte	0
	.uleb128 0x5e
	.4byte	0x3658
	.4byte	.LFB208
	.4byte	.LFE208-.LFB208
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x638e
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1a8
	.uleb128 0x63
	.4byte	0x3666
	.4byte	.LLST186
	.uleb128 0x63
	.4byte	0x3672
	.4byte	.LLST187
	.uleb128 0x63
	.4byte	0x367e
	.4byte	.LLST188
	.uleb128 0x62
	.4byte	0x368a
	.uleb128 0x3
	.byte	0x91
	.sleb128 -68
	.uleb128 0x30
	.4byte	0x3696
	.uleb128 0x24
	.4byte	.LVL916
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL917
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL920
	.4byte	0x17d1
	.uleb128 0x24
	.4byte	.LVL922
	.4byte	0x23f1
	.uleb128 0x24
	.4byte	.LVL923
	.4byte	0x19f5
	.uleb128 0x24
	.4byte	.LVL924
	.4byte	0x233b
	.byte	0
	.byte	0
	.uleb128 0x5e
	.4byte	0x2b80
	.4byte	.LFB226
	.4byte	.LFE226-.LFB226
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x63ab
	.uleb128 0x24
	.4byte	.LVL1124
	.4byte	0x3855
	.byte	0
	.uleb128 0x5e
	.4byte	0x343a
	.4byte	.LFB211
	.4byte	.LFE211-.LFB211
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x63da
	.uleb128 0x24
	.4byte	.LVL1148
	.4byte	0x3781
	.uleb128 0x24
	.4byte	.LVL1149
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1150
	.4byte	0x17d1
	.byte	0
	.uleb128 0x5e
	.4byte	0x1f7c
	.4byte	.LFB251
	.4byte	.LFE251-.LFB251
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6409
	.uleb128 0x24
	.4byte	.LVL1272
	.4byte	0x1f85
	.uleb128 0x24
	.4byte	.LVL1273
	.4byte	0x36a4
	.uleb128 0x24
	.4byte	.LVL1274
	.4byte	0x20d1
	.byte	0
	.uleb128 0x5e
	.4byte	0x1c7f
	.4byte	.LFB259
	.4byte	.LFE259-.LFB259
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x65fa
	.uleb128 0x5f
	.4byte	0x1c90
	.4byte	.LLST245
	.uleb128 0x5f
	.4byte	0x1c9c
	.4byte	.LLST246
	.uleb128 0x30
	.4byte	0x1ca8
	.uleb128 0x30
	.4byte	0x1cb4
	.uleb128 0x30
	.4byte	0x1cc0
	.uleb128 0x30
	.4byte	0x1ccc
	.uleb128 0x30
	.4byte	0x1cd8
	.uleb128 0x30
	.4byte	0x1ce4
	.uleb128 0x30
	.4byte	0x1cf0
	.uleb128 0x30
	.4byte	0x1cfc
	.uleb128 0x30
	.4byte	0x1d08
	.uleb128 0x30
	.4byte	0x1d14
	.uleb128 0x30
	.4byte	0x1d30
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x1f8
	.4byte	0x65f0
	.uleb128 0x2e
	.4byte	0x1c9c
	.uleb128 0x2e
	.4byte	0x1c90
	.uleb128 0x2f
	.4byte	.Ldebug_ranges0+0x1f8
	.uleb128 0x63
	.4byte	0x642e
	.4byte	.LLST247
	.uleb128 0x63
	.4byte	0x6433
	.4byte	.LLST248
	.uleb128 0x63
	.4byte	0x6438
	.4byte	.LLST249
	.uleb128 0x30
	.4byte	0x643d
	.uleb128 0x30
	.4byte	0x6442
	.uleb128 0x30
	.4byte	0x6447
	.uleb128 0x63
	.4byte	0x644c
	.4byte	.LLST250
	.uleb128 0x63
	.4byte	0x6451
	.4byte	.LLST251
	.uleb128 0x63
	.4byte	0x6456
	.4byte	.LLST252
	.uleb128 0x63
	.4byte	0x645b
	.4byte	.LLST253
	.uleb128 0x64
	.4byte	0x1d20
	.uleb128 0x64
	.4byte	0x1d28
	.uleb128 0x30
	.4byte	0x6460
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x218
	.4byte	0x6516
	.uleb128 0x30
	.4byte	0x1d3e
	.uleb128 0x24
	.4byte	.LVL1299
	.4byte	0x269f
	.uleb128 0x24
	.4byte	.LVL1300
	.4byte	0x3fa5
	.uleb128 0x24
	.4byte	.LVL1301
	.4byte	0x3fa5
	.uleb128 0x24
	.4byte	.LVL1302
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1304
	.4byte	0x1e11
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1316
	.4byte	0x2262
	.uleb128 0x24
	.4byte	.LVL1317
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1322
	.4byte	0x1d67
	.uleb128 0x24
	.4byte	.LVL1323
	.4byte	0x3fa5
	.uleb128 0x24
	.4byte	.LVL1324
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1326
	.4byte	0x2262
	.uleb128 0x24
	.4byte	.LVL1327
	.4byte	0x233b
	.uleb128 0x24
	.4byte	.LVL1328
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1329
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1330
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1331
	.4byte	0x2c52
	.uleb128 0x24
	.4byte	.LVL1333
	.4byte	0x1d67
	.uleb128 0x24
	.4byte	.LVL1353
	.4byte	0x1b05
	.uleb128 0x24
	.4byte	.LVL1357
	.4byte	0x6727
	.uleb128 0x24
	.4byte	.LVL1358
	.4byte	0x392c
	.uleb128 0x24
	.4byte	.LVL1360
	.4byte	0x1f22
	.uleb128 0x24
	.4byte	.LVL1363
	.4byte	0x2294
	.uleb128 0x24
	.4byte	.LVL1364
	.4byte	0x1eae
	.uleb128 0x24
	.4byte	.LVL1373
	.4byte	0x1eae
	.uleb128 0x24
	.4byte	.LVL1376
	.4byte	0x23b1
	.uleb128 0x24
	.4byte	.LVL1377
	.4byte	0x4f6d
	.uleb128 0x24
	.4byte	.LVL1378
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1379
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1383
	.4byte	0x3fa5
	.byte	0
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1303
	.4byte	0x20d1
	.byte	0
	.uleb128 0x5e
	.4byte	0x2812
	.4byte	.LFB232
	.4byte	.LFE232-.LFB232
	.uleb128 0x1
	.byte	0x9c
	.4byte	0x6708
	.uleb128 0x30
	.4byte	0x2823
	.uleb128 0x52
	.4byte	.LBB521
	.4byte	.LBE521-.LBB521
	.4byte	0x66a4
	.uleb128 0x63
	.4byte	0x660d
	.4byte	.LLST287
	.uleb128 0x38
	.4byte	.Ldebug_ranges0+0x238
	.4byte	0x6637
	.uleb128 0x30
	.4byte	0x282e
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1592
	.4byte	0x2a91
	.uleb128 0x24
	.4byte	.LVL1593
	.4byte	0x2a91
	.uleb128 0x24
	.4byte	.LVL1594
	.4byte	0x36a4
	.uleb128 0x24
	.4byte	.LVL1595
	.4byte	0x36a4
	.uleb128 0x24
	.4byte	.LVL1596
	.4byte	0x39f7
	.uleb128 0x24
	.4byte	.LVL1597
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1598
	.4byte	0x3593
	.uleb128 0x24
	.4byte	.LVL1601
	.4byte	0x1e7a
	.uleb128 0x24
	.4byte	.LVL1602
	.4byte	0x1e7a
	.uleb128 0x24
	.4byte	.LVL1603
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1604
	.4byte	0x246d
	.uleb128 0x24
	.4byte	.LVL1605
	.4byte	0x29d5
	.byte	0
	.uleb128 0x24
	.4byte	.LVL1580
	.4byte	0x4400
	.uleb128 0x24
	.4byte	.LVL1581
	.4byte	0x3447
	.uleb128 0x24
	.4byte	.LVL1582
	.4byte	0x333a
	.uleb128 0x24
	.4byte	.LVL1583
	.4byte	0x3198
	.uleb128 0x24
	.4byte	.LVL1584
	.4byte	0x31fa
	.uleb128 0x24
	.4byte	.LVL1585
	.4byte	0x2b06
	.uleb128 0x24
	.4byte	.LVL1586
	.4byte	0x343a
	.uleb128 0x24
	.4byte	.LVL1587
	.4byte	0x42f5
	.uleb128 0x24
	.4byte	.LVL1588
	.4byte	0x2b89
	.uleb128 0x24
	.4byte	.LVL1589
	.4byte	0x2cbd
	.uleb128 0x24
	.4byte	.LVL1590
	.4byte	0x3710
	.byte	0
	.uleb128 0x65
	.4byte	.LASF658
	.4byte	.LASF658
	.byte	0x12
	.byte	0x6f
	.uleb128 0x66
	.4byte	.LASF671
	.4byte	.LASF671
	.uleb128 0x65
	.4byte	.LASF659
	.4byte	.LASF659
	.byte	0x13
	.byte	0x19
	.uleb128 0x65
	.4byte	.LASF660
	.4byte	.LASF660
	.byte	0xc
	.byte	0x8e
	.uleb128 0x65
	.4byte	.LASF661
	.4byte	.LASF661
	.byte	0x13
	.byte	0x1b
	.uleb128 0x65
	.4byte	.LASF662
	.4byte	.LASF662
	.byte	0x14
	.byte	0x2c
	.uleb128 0x65
	.4byte	.LASF663
	.4byte	.LASF663
	.byte	0xe
	.byte	0x18
	.uleb128 0x67
	.4byte	.LASF664
	.4byte	.LASF664
	.byte	0xd
	.2byte	0x19b
	.uleb128 0x68
	.4byte	.LASF672
	.4byte	.LASF673
	.byte	0x15
	.byte	0
	.4byte	.LASF672
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
	.byte	0
	.byte	0
	.uleb128 0xb
	.uleb128 0x15
	.byte	0
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
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
	.uleb128 0x59
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
	.uleb128 0x5a
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
	.uleb128 0x5b
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
	.uleb128 0x5c
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
	.uleb128 0x5e
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
	.uleb128 0x5f
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x60
	.uleb128 0x5
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x61
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
	.uleb128 0x62
	.uleb128 0x34
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
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
	.uleb128 0xa
	.byte	0
	.uleb128 0x31
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x65
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
	.uleb128 0x66
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
	.uleb128 0x5
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
	.uleb128 0x6e
	.uleb128 0xe
	.byte	0
	.byte	0
	.byte	0
	.section	.debug_loc,"",%progbits
.Ldebug_loc0:
.LLST290:
	.4byte	.LVL1645-.Ltext0
	.4byte	.LVL1646-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST291:
	.4byte	.LVL1645-.Ltext0
	.4byte	.LVL1646-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST292:
	.4byte	.LVL1645-.Ltext0
	.4byte	.LVL1646-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST170:
	.4byte	.LVL834-.Ltext0
	.4byte	.LVL835-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST171:
	.4byte	.LVL834-.Ltext0
	.4byte	.LVL835-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST172:
	.4byte	.LVL834-.Ltext0
	.4byte	.LVL835-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST104:
	.4byte	.LVL466-.Ltext0
	.4byte	.LVL468-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL468-1-.Ltext0
	.4byte	.LFE266-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST105:
	.4byte	.LVL466-.Ltext0
	.4byte	.LVL467-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL467-.Ltext0
	.4byte	.LFE266-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST106:
	.4byte	.LVL470-.Ltext0
	.4byte	.LVL471-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL473-.Ltext0
	.4byte	.LVL474-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL475-.Ltext0
	.4byte	.LVL476-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST91:
	.4byte	.LVL395-.Ltext0
	.4byte	.LVL399-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL400-.Ltext0
	.4byte	.LVL402-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST92:
	.4byte	.LVL395-.Ltext0
	.4byte	.LVL401-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST93:
	.4byte	.LVL395-.Ltext0
	.4byte	.LVL400-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL400-.Ltext0
	.4byte	.LFE264-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST86:
	.4byte	.LVL370-.Ltext0
	.4byte	.LVL372-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST87:
	.4byte	.LVL370-.Ltext0
	.4byte	.LVL375-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL375-.Ltext0
	.4byte	.LVL385-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL385-.Ltext0
	.4byte	.LVL387-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL392-.Ltext0
	.4byte	.LFE263-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST88:
	.4byte	.LVL370-.Ltext0
	.4byte	.LVL373-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL373-.Ltext0
	.4byte	.LFE263-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST89:
	.4byte	.LVL370-.Ltext0
	.4byte	.LVL371-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL371-.Ltext0
	.4byte	.LVL384-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL393-.Ltext0
	.4byte	.LVL394-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST90:
	.4byte	.LVL373-.Ltext0
	.4byte	.LVL376-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL377-.Ltext0
	.4byte	.LVL378-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL381-.Ltext0
	.4byte	.LVL382-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL384-.Ltext0
	.4byte	.LVL386-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL392-.Ltext0
	.4byte	.LFE263-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST79:
	.4byte	.LVL340-.Ltext0
	.4byte	.LVL344-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL345-.Ltext0
	.4byte	.LVL347-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL348-.Ltext0
	.4byte	.LVL350-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST80:
	.4byte	.LVL340-.Ltext0
	.4byte	.LVL345-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL345-.Ltext0
	.4byte	.LFE262-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST81:
	.4byte	.LVL340-.Ltext0
	.4byte	.LVL346-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL348-.Ltext0
	.4byte	.LVL349-.Ltext0
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
	.4byte	.LFE261-.Ltext0
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
	.4byte	.LFE261-.Ltext0
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
	.4byte	.LFE261-.Ltext0
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
.LLST78:
	.4byte	.LVL334-.Ltext0
	.4byte	.LVL335-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL335-1-.Ltext0
	.4byte	.LFE255-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST77:
	.4byte	.LVL331-.Ltext0
	.4byte	.LVL332-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL332-.Ltext0
	.4byte	.LFE254-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST244:
	.4byte	.LVL1287-.Ltext0
	.4byte	.LVL1288-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST161:
	.4byte	.LVL794-.Ltext0
	.4byte	.LVL798-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL798-.Ltext0
	.4byte	.LVL833-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST162:
	.4byte	.LVL794-.Ltext0
	.4byte	.LVL796-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL796-.Ltext0
	.4byte	.LVL797-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL798-.Ltext0
	.4byte	.LVL799-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL799-.Ltext0
	.4byte	.LVL801-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL805-.Ltext0
	.4byte	.LVL808-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL831-.Ltext0
	.4byte	.LVL832-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST163:
	.4byte	.LVL802-.Ltext0
	.4byte	.LVL803-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL811-.Ltext0
	.4byte	.LVL812-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL815-.Ltext0
	.4byte	.LVL816-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST164:
	.4byte	.LVL800-.Ltext0
	.4byte	.LVL813-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL814-.Ltext0
	.4byte	.LVL833-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST165:
	.4byte	.LVL809-.Ltext0
	.4byte	.LVL810-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL822-.Ltext0
	.4byte	.LVL824-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST166:
	.4byte	.LVL795-.Ltext0
	.4byte	.LVL806-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL807-.Ltext0
	.4byte	.LVL828-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL831-.Ltext0
	.4byte	.LVL833-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST167:
	.4byte	.LVL817-.Ltext0
	.4byte	.LVL818-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL819-.Ltext0
	.4byte	.LVL823-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL826-.Ltext0
	.4byte	.LVL827-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST168:
	.4byte	.LVL800-.Ltext0
	.4byte	.LVL833-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST169:
	.4byte	.LVL820-.Ltext0
	.4byte	.LVL821-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL826-.Ltext0
	.4byte	.LVL827-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST240:
	.4byte	.LVL1240-.Ltext0
	.4byte	.LVL1243-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1244-.Ltext0
	.4byte	.LVL1245-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1246-.Ltext0
	.4byte	.LVL1247-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1255-.Ltext0
	.4byte	.LVL1256-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST241:
	.4byte	.LVL1242-.Ltext0
	.4byte	.LVL1244-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1249-.Ltext0
	.4byte	.LVL1253-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1254-.Ltext0
	.4byte	.LFE248-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	0
	.4byte	0
.LLST242:
	.4byte	.LVL1241-.Ltext0
	.4byte	.LVL1248-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1253-.Ltext0
	.4byte	.LVL1254-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1255-.Ltext0
	.4byte	.LVL1256-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST243:
	.4byte	.LVL1258-.Ltext0
	.4byte	.LVL1260-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1264-.Ltext0
	.4byte	.LVL1270-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST72:
	.4byte	.LVL320-.Ltext0
	.4byte	.LVL321-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL321-.Ltext0
	.4byte	.LFE247-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST73:
	.4byte	.LVL320-.Ltext0
	.4byte	.LVL322-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL322-1-.Ltext0
	.4byte	.LVL330-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST74:
	.4byte	.LVL320-.Ltext0
	.4byte	.LVL322-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL322-1-.Ltext0
	.4byte	.LVL330-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST75:
	.4byte	.LVL323-.Ltext0
	.4byte	.LVL329-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST76:
	.4byte	.LVL324-.Ltext0
	.4byte	.LVL325-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL326-.Ltext0
	.4byte	.LVL327-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL328-.Ltext0
	.4byte	.LFE247-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST71:
	.4byte	.LVL316-.Ltext0
	.4byte	.LVL317-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL318-.Ltext0
	.4byte	.LVL319-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST185:
	.4byte	.LVL910-.Ltext0
	.4byte	.LVL913-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL913-.Ltext0
	.4byte	.LFE242-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST69:
	.4byte	.LVL306-.Ltext0
	.4byte	.LVL307-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL307-.Ltext0
	.4byte	.LFE240-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST70:
	.4byte	.LVL310-.Ltext0
	.4byte	.LVL311-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL313-.Ltext0
	.4byte	.LFE240-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST226:
	.4byte	.LVL1181-.Ltext0
	.4byte	.LVL1183-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1183-.Ltext0
	.4byte	.LFE239-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST177:
	.4byte	.LVL854-.Ltext0
	.4byte	.LVL857-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL857-.Ltext0
	.4byte	.LVL888-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST178:
	.4byte	.LVL855-.Ltext0
	.4byte	.LVL888-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST179:
	.4byte	.LVL855-.Ltext0
	.4byte	.LVL856-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL869-.Ltext0
	.4byte	.LVL880-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL881-.Ltext0
	.4byte	.LVL888-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST180:
	.4byte	.LVL875-.Ltext0
	.4byte	.LVL876-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL877-.Ltext0
	.4byte	.LVL878-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL883-.Ltext0
	.4byte	.LVL884-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL885-.Ltext0
	.4byte	.LVL886-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST181:
	.4byte	.LVL858-.Ltext0
	.4byte	.LVL859-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL860-.Ltext0
	.4byte	.LVL861-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL862-.Ltext0
	.4byte	.LVL863-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST64:
	.4byte	.LVL286-.Ltext0
	.4byte	.LVL287-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL291-.Ltext0
	.4byte	.LFE236-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST65:
	.4byte	.LVL286-.Ltext0
	.4byte	.LVL287-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL290-.Ltext0
	.4byte	.LVL292-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL300-.Ltext0
	.4byte	.LVL301-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL302-.Ltext0
	.4byte	.LFE236-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST66:
	.4byte	.LVL286-.Ltext0
	.4byte	.LVL287-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL289-.Ltext0
	.4byte	.LFE236-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST67:
	.4byte	.LVL286-.Ltext0
	.4byte	.LVL287-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL293-.Ltext0
	.4byte	.LVL294-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL295-.Ltext0
	.4byte	.LVL296-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL297-.Ltext0
	.4byte	.LVL298-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST59:
	.4byte	.LVL198-.Ltext0
	.4byte	.LVL199-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL200-.Ltext0
	.4byte	.LVL201-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST58:
	.4byte	.LVL193-.Ltext0
	.4byte	.LVL195-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL195-.Ltext0
	.4byte	.LFE233-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST142:
	.4byte	.LVL646-.Ltext0
	.4byte	.LVL649-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL654-.Ltext0
	.4byte	.LVL657-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST143:
	.4byte	.LVL650-.Ltext0
	.4byte	.LVL654-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL657-.Ltext0
	.4byte	.LFE230-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST144:
	.4byte	.LVL651-.Ltext0
	.4byte	.LVL654-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST224:
	.4byte	.LVL1171-.Ltext0
	.4byte	.LVL1173-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1176-.Ltext0
	.4byte	.LFE229-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST225:
	.4byte	.LVL1168-.Ltext0
	.4byte	.LVL1172-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1173-.Ltext0
	.4byte	.LVL1176-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST205:
	.4byte	.LVL1046-.Ltext0
	.4byte	.LVL1047-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1047-.Ltext0
	.4byte	.LFE228-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST158:
	.4byte	.LVL778-.Ltext0
	.4byte	.LVL790-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST159:
	.4byte	.LVL776-.Ltext0
	.4byte	.LVL785-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL786-.Ltext0
	.4byte	.LVL788-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST160:
	.4byte	.LVL775-.Ltext0
	.4byte	.LVL782-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL783-.Ltext0
	.4byte	.LVL790-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST57:
	.4byte	.LVL188-.Ltext0
	.4byte	.LVL189-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL189-.Ltext0
	.4byte	.LFE224-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST190:
	.4byte	.LVL930-.Ltext0
	.4byte	.LVL935-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL935-.Ltext0
	.4byte	.LFE221-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST191:
	.4byte	.LVL931-.Ltext0
	.4byte	.LVL932-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL933-.Ltext0
	.4byte	.LVL938-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL939-.Ltext0
	.4byte	.LVL940-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL952-.Ltext0
	.4byte	.LVL955-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL957-.Ltext0
	.4byte	.LVL958-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL958-.Ltext0
	.4byte	.LVL959-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL969-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL972-.Ltext0
	.4byte	.LVL973-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL992-.Ltext0
	.4byte	.LVL994-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1000-.Ltext0
	.4byte	.LVL1001-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST192:
	.4byte	.LVL932-.Ltext0
	.4byte	.LVL934-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL951-.Ltext0
	.4byte	.LVL954-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL959-.Ltext0
	.4byte	.LVL960-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL960-.Ltext0
	.4byte	.LVL961-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL974-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL976-.Ltext0
	.4byte	.LVL979-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL983-.Ltext0
	.4byte	.LVL984-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL993-.Ltext0
	.4byte	.LVL996-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1000-.Ltext0
	.4byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1043-.Ltext0
	.4byte	.LVL1045-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST193:
	.4byte	.LVL950-.Ltext0
	.4byte	.LVL955-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL967-.Ltext0
	.4byte	.LVL968-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL995-.Ltext0
	.4byte	.LVL997-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1043-.Ltext0
	.4byte	.LVL1044-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST194:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL948-.Ltext0
	.4byte	.LVL956-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL965-.Ltext0
	.4byte	.LVL966-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL967-.Ltext0
	.4byte	.LFE221-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST195:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL941-.Ltext0
	.4byte	.LVL942-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL942-.Ltext0
	.4byte	.LVL970-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL977-.Ltext0
	.4byte	.LVL978-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST196:
	.4byte	.LVL946-.Ltext0
	.4byte	.LVL947-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL962-.Ltext0
	.4byte	.LVL963-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST197:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL981-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL983-.Ltext0
	.4byte	.LVL985-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL988-.Ltext0
	.4byte	.LVL990-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL998-.Ltext0
	.4byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1010-.Ltext0
	.4byte	.LVL1017-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1019-.Ltext0
	.4byte	.LVL1036-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST198:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -60
	.4byte	.LVL953-.Ltext0
	.4byte	.LVL956-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -60
	.4byte	.LVL980-.Ltext0
	.4byte	.LFE221-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -60
	.4byte	0
	.4byte	0
.LLST199:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	.LVL953-.Ltext0
	.4byte	.LVL956-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	.LVL980-.Ltext0
	.4byte	.LFE221-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -56
	.4byte	0
	.4byte	0
.LLST200:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL953-.Ltext0
	.4byte	.LVL956-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL980-.Ltext0
	.4byte	.LFE221-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	0
	.4byte	0
.LLST201:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL981-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL983-.Ltext0
	.4byte	.LVL985-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL988-.Ltext0
	.4byte	.LVL990-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL999-.Ltext0
	.4byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1017-.Ltext0
	.4byte	.LVL1019-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1036-.Ltext0
	.4byte	.LVL1038-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST202:
	.4byte	.LVL944-.Ltext0
	.4byte	.LVL945-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL971-.Ltext0
	.4byte	.LVL972-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL983-.Ltext0
	.4byte	.LVL984-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1000-.Ltext0
	.4byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1003-.Ltext0
	.4byte	.LVL1004-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1005-.Ltext0
	.4byte	.LVL1006-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1007-.Ltext0
	.4byte	.LVL1008-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1040-.Ltext0
	.4byte	.LVL1041-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1041-.Ltext0
	.4byte	.LVL1042-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1042-.Ltext0
	.4byte	.LVL1043-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST203:
	.4byte	.LVL1021-.Ltext0
	.4byte	.LVL1022-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST204:
	.4byte	.LVL936-.Ltext0
	.4byte	.LVL937-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL980-.Ltext0
	.4byte	.LVL986-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL987-.Ltext0
	.4byte	.LVL989-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL999-.Ltext0
	.4byte	.LVL1002-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1014-.Ltext0
	.4byte	.LVL1019-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1023-.Ltext0
	.4byte	.LVL1025-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1027-.Ltext0
	.4byte	.LVL1030-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1036-.Ltext0
	.4byte	.LVL1038-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST136:
	.4byte	.LVL613-.Ltext0
	.4byte	.LVL614-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL614-.Ltext0
	.4byte	.LVL631-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST137:
	.4byte	.LVL613-.Ltext0
	.4byte	.LVL615-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL615-1-.Ltext0
	.4byte	.LFE220-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST138:
	.4byte	.LVL616-.Ltext0
	.4byte	.LVL620-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL624-.Ltext0
	.4byte	.LVL629-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST139:
	.4byte	.LVL618-.Ltext0
	.4byte	.LVL626-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL627-.Ltext0
	.4byte	.LVL631-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST140:
	.4byte	.LVL617-.Ltext0
	.4byte	.LVL619-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL625-.Ltext0
	.4byte	.LVL628-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST55:
	.4byte	.LVL179-.Ltext0
	.4byte	.LVL180-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL182-.Ltext0
	.4byte	.LVL185-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST56:
	.4byte	.LVL179-.Ltext0
	.4byte	.LVL181-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL182-.Ltext0
	.4byte	.LVL183-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL184-.Ltext0
	.4byte	.LVL185-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST150:
	.4byte	.LVL708-.Ltext0
	.4byte	.LVL714-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL714-.Ltext0
	.4byte	.LFE215-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST151:
	.4byte	.LVL719-.Ltext0
	.4byte	.LVL724-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL727-.Ltext0
	.4byte	.LVL729-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL732-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL746-.Ltext0
	.4byte	.LVL747-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST152:
	.4byte	.LVL726-.Ltext0
	.4byte	.LVL731-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST153:
	.4byte	.LVL709-.Ltext0
	.4byte	.LVL711-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL717-.Ltext0
	.4byte	.LVL721-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL721-.Ltext0
	.4byte	.LVL722-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL730-.Ltext0
	.4byte	.LVL733-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL733-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL738-.Ltext0
	.4byte	.LVL740-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL744-.Ltext0
	.4byte	.LVL748-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL748-.Ltext0
	.4byte	.LVL749-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL751-.Ltext0
	.4byte	.LVL753-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL757-.Ltext0
	.4byte	.LVL759-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL760-.Ltext0
	.4byte	.LVL761-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL762-.Ltext0
	.4byte	.LFE215-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST154:
	.4byte	.LVL710-.Ltext0
	.4byte	.LVL728-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL732-.Ltext0
	.4byte	.LVL738-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL746-.Ltext0
	.4byte	.LFE215-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST155:
	.4byte	.LVL711-.Ltext0
	.4byte	.LVL712-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL725-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL738-.Ltext0
	.4byte	.LVL739-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST156:
	.4byte	.LVL712-.Ltext0
	.4byte	.LVL713-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL715-.Ltext0
	.4byte	.LVL716-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL717-.Ltext0
	.4byte	.LVL723-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL730-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL734-1-.Ltext0
	.4byte	.LVL738-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL738-.Ltext0
	.4byte	.LVL739-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL742-.Ltext0
	.4byte	.LVL743-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL745-.Ltext0
	.4byte	.LVL746-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL746-.Ltext0
	.4byte	.LVL751-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL751-.Ltext0
	.4byte	.LVL752-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL755-.Ltext0
	.4byte	.LVL756-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL758-.Ltext0
	.4byte	.LVL759-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL759-.Ltext0
	.4byte	.LFE215-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST157:
	.4byte	.LVL713-.Ltext0
	.4byte	.LVL715-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL718-.Ltext0
	.4byte	.LVL720-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL732-.Ltext0
	.4byte	.LVL734-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST286:
	.4byte	.LVL1563-.Ltext0
	.4byte	.LVL1566-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1576-.Ltext0
	.4byte	.LVL1578-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1579-.Ltext0
	.4byte	.LFE214-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST54:
	.4byte	.LVL177-.Ltext0
	.4byte	.LVL178-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST147:
	.4byte	.LVL672-.Ltext0
	.4byte	.LVL674-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL675-.Ltext0
	.4byte	.LVL677-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL690-.Ltext0
	.4byte	.LVL691-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL692-.Ltext0
	.4byte	.LVL693-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL703-.Ltext0
	.4byte	.LVL704-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL706-.Ltext0
	.4byte	.LVL707-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST148:
	.4byte	.LVL673-.Ltext0
	.4byte	.LVL675-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL680-.Ltext0
	.4byte	.LVL681-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL682-.Ltext0
	.4byte	.LVL683-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL684-.Ltext0
	.4byte	.LVL685-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL686-.Ltext0
	.4byte	.LVL687-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL688-.Ltext0
	.4byte	.LVL690-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL690-.Ltext0
	.4byte	.LVL693-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL693-.Ltext0
	.4byte	.LVL694-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL696-.Ltext0
	.4byte	.LVL697-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST149:
	.4byte	.LVL673-.Ltext0
	.4byte	.LVL676-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL689-.Ltext0
	.4byte	.LVL693-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL695-.Ltext0
	.4byte	.LFE210-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST223:
	.4byte	.LVL1159-.Ltext0
	.4byte	.LVL1164-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1165-.Ltext0
	.4byte	.LFE209-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST145:
	.4byte	.LVL661-.Ltext0
	.4byte	.LVL662-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL665-.Ltext0
	.4byte	.LVL666-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST146:
	.4byte	.LVL660-.Ltext0
	.4byte	.LVL661-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST216:
	.4byte	.LVL1125-.Ltext0
	.4byte	.LVL1129-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1131-.Ltext0
	.4byte	.LVL1133-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST217:
	.4byte	.LVL1126-.Ltext0
	.4byte	.LVL1127-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1128-.Ltext0
	.4byte	.LVL1130-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1131-.Ltext0
	.4byte	.LVL1132-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST218:
	.4byte	.LVL1136-.Ltext0
	.4byte	.LVL1141-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1141-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST219:
	.4byte	.LVL1136-.Ltext0
	.4byte	.LVL1141-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1141-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST220:
	.4byte	.LVL1136-.Ltext0
	.4byte	.LVL1137-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1137-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST221:
	.4byte	.LVL1140-.Ltext0
	.4byte	.LVL1142-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL1144-.Ltext0
	.4byte	.LFE204-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST222:
	.4byte	.LVL1138-.Ltext0
	.4byte	.LVL1139-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST211:
	.4byte	.LVL1104-.Ltext0
	.4byte	.LVL1108-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1108-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST212:
	.4byte	.LVL1104-.Ltext0
	.4byte	.LVL1108-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1108-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST213:
	.4byte	.LVL1104-.Ltext0
	.4byte	.LVL1105-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1107-.Ltext0
	.4byte	.LVL1109-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1119-.Ltext0
	.4byte	.LVL1120-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1120-.Ltext0
	.4byte	.LVL1121-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST214:
	.4byte	.LVL1107-.Ltext0
	.4byte	.LVL1113-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1114-.Ltext0
	.4byte	.LFE203-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST215:
	.4byte	.LVL1110-.Ltext0
	.4byte	.LVL1111-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1112-.Ltext0
	.4byte	.LVL1115-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1121-.Ltext0
	.4byte	.LVL1122-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST132:
	.4byte	.LVL600-.Ltext0
	.4byte	.LVL603-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL604-.Ltext0
	.4byte	.LVL606-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL609-.Ltext0
	.4byte	.LVL610-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST133:
	.4byte	.LVL600-.Ltext0
	.4byte	.LVL604-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL604-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST134:
	.4byte	.LVL600-.Ltext0
	.4byte	.LVL604-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL604-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST135:
	.4byte	.LVL605-.Ltext0
	.4byte	.LVL611-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL612-.Ltext0
	.4byte	.LFE202-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST189:
	.4byte	.LVL926-.Ltext0
	.4byte	.LVL927-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL928-.Ltext0
	.4byte	.LVL929-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL929-.Ltext0
	.4byte	.LFE201-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST131:
	.4byte	.LVL597-.Ltext0
	.4byte	.LVL598-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST125:
	.4byte	.LVL570-.Ltext0
	.4byte	.LVL572-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL572-.Ltext0
	.4byte	.LFE198-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST126:
	.4byte	.LVL570-.Ltext0
	.4byte	.LVL572-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL572-.Ltext0
	.4byte	.LFE198-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST127:
	.4byte	.LVL576-.Ltext0
	.4byte	.LVL577-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST128:
	.4byte	.LVL571-.Ltext0
	.4byte	.LVL579-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL580-.Ltext0
	.4byte	.LFE198-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST121:
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
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST122:
	.4byte	.LVL563-.Ltext0
	.4byte	.LVL564-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL567-.Ltext0
	.4byte	.LVL568-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL569-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST123:
	.4byte	.LVL554-.Ltext0
	.4byte	.LVL560-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST124:
	.4byte	.LVL557-.Ltext0
	.4byte	.LVL558-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL559-.Ltext0
	.4byte	.LFE197-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST129:
	.4byte	.LVL583-.Ltext0
	.4byte	.LVL586-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL586-1-.Ltext0
	.4byte	.LFE196-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST130:
	.4byte	.LVL584-.Ltext0
	.4byte	.LVL589-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL591-.Ltext0
	.4byte	.LVL593-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST116:
	.4byte	.LVL536-.Ltext0
	.4byte	.LVL540-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL540-.Ltext0
	.4byte	.LFE194-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST117:
	.4byte	.LVL539-.Ltext0
	.4byte	.LVL550-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST118:
	.4byte	.LVL542-.Ltext0
	.4byte	.LVL545-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL546-.Ltext0
	.4byte	.LVL547-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL548-.Ltext0
	.4byte	.LVL549-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST119:
	.4byte	.LVL543-.Ltext0
	.4byte	.LVL546-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL549-.Ltext0
	.4byte	.LVL551-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL552-.Ltext0
	.4byte	.LFE194-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST120:
	.4byte	.LVL544-.Ltext0
	.4byte	.LVL546-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL549-.Ltext0
	.4byte	.LFE194-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST113:
	.4byte	.LVL524-.Ltext0
	.4byte	.LVL525-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL525-.Ltext0
	.4byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST114:
	.4byte	.LVL524-.Ltext0
	.4byte	.LVL525-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL525-.Ltext0
	.4byte	.LVL531-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST115:
	.4byte	.LVL528-.Ltext0
	.4byte	.LVL529-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL531-.Ltext0
	.4byte	.LFE193-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST46:
	.4byte	.LVL146-.Ltext0
	.4byte	.LVL148-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL149-.Ltext0
	.4byte	.LVL150-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL152-.Ltext0
	.4byte	.LVL155-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST47:
	.4byte	.LVL147-.Ltext0
	.4byte	.LVL149-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL151-.Ltext0
	.4byte	.LVL153-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL154-.Ltext0
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
	.4byte	.LFE189-.Ltext0
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
	.4byte	.LFE189-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST40:
	.4byte	.LVL128-.Ltext0
	.4byte	.LVL131-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL131-.Ltext0
	.4byte	.LFE188-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST41:
	.4byte	.LVL128-.Ltext0
	.4byte	.LVL130-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL131-.Ltext0
	.4byte	.LVL132-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL133-.Ltext0
	.4byte	.LVL134-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST37:
	.4byte	.LVL116-.Ltext0
	.4byte	.LVL120-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL121-.Ltext0
	.4byte	.LFE187-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST38:
	.4byte	.LVL117-.Ltext0
	.4byte	.LVL122-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL123-.Ltext0
	.4byte	.LVL124-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST30:
	.4byte	.LVL90-.Ltext0
	.4byte	.LVL109-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL110-.Ltext0
	.4byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST31:
	.4byte	.LVL91-.Ltext0
	.4byte	.LVL102-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL103-.Ltext0
	.4byte	.LVL104-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL105-.Ltext0
	.4byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST32:
	.4byte	.LVL92-.Ltext0
	.4byte	.LVL107-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL108-.Ltext0
	.4byte	.LVL111-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL112-.Ltext0
	.4byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST33:
	.4byte	.LVL93-.Ltext0
	.4byte	.LVL95-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST34:
	.4byte	.LVL94-.Ltext0
	.4byte	.LVL101-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL105-.Ltext0
	.4byte	.LFE186-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST35:
	.4byte	.LVL97-.Ltext0
	.4byte	.LVL99-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL100-.Ltext0
	.4byte	.LVL106-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL108-.Ltext0
	.4byte	.LVL110-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST29:
	.4byte	.LVL85-.Ltext0
	.4byte	.LVL87-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL88-.Ltext0
	.4byte	.LVL89-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST26:
	.4byte	.LVL72-.Ltext0
	.4byte	.LVL74-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL79-.Ltext0
	.4byte	.LVL81-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL83-.Ltext0
	.4byte	.LFE181-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST27:
	.4byte	.LVL72-.Ltext0
	.4byte	.LVL75-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL77-.Ltext0
	.4byte	.LVL78-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	.LVL82-.Ltext0
	.4byte	.LVL84-.Ltext0
	.2byte	0x1
	.byte	0x5c
	.4byte	0
	.4byte	0
.LLST28:
	.4byte	.LVL72-.Ltext0
	.4byte	.LVL73-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	.LVL80-.Ltext0
	.4byte	.LVL84-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	0
	.4byte	0
.LLST94:
	.4byte	.LVL406-.Ltext0
	.4byte	.LVL409-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL409-.Ltext0
	.4byte	.LFE180-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST95:
	.4byte	.LVL406-.Ltext0
	.4byte	.LVL407-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL409-.Ltext0
	.4byte	.LFE180-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST25:
	.4byte	.LVL69-.Ltext0
	.4byte	.LVL70-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST84:
	.4byte	.LVL362-.Ltext0
	.4byte	.LVL364-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST85:
	.4byte	.LVL362-.Ltext0
	.4byte	.LVL363-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL363-.Ltext0
	.4byte	.LFE176-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST24:
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
.LLST23:
	.4byte	.LVL62-.Ltext0
	.4byte	.LVL63-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST22:
	.4byte	.LVL59-.Ltext0
	.4byte	.LVL60-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL60-1-.Ltext0
	.4byte	.LFE173-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST20:
	.4byte	.LVL54-.Ltext0
	.4byte	.LVL56-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL56-1-.Ltext0
	.4byte	.LFE172-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST21:
	.4byte	.LVL54-.Ltext0
	.4byte	.LVL55-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL55-.Ltext0
	.4byte	.LVL57-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST209:
	.4byte	.LVL1071-.Ltext0
	.4byte	.LVL1103-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST210:
	.4byte	.LVL1074-.Ltext0
	.4byte	.LVL1075-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1079-.Ltext0
	.4byte	.LVL1080-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1081-.Ltext0
	.4byte	.LVL1090-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1091-.Ltext0
	.4byte	.LVL1097-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST82:
	.4byte	.LVL355-.Ltext0
	.4byte	.LVL356-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL357-.Ltext0
	.4byte	.LVL358-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL359-.Ltext0
	.4byte	.LFE170-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST83:
	.4byte	.LVL360-.Ltext0
	.4byte	.LVL361-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST206:
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
.LLST207:
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
	.4byte	.LFE169-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST208:
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
.LLST19:
	.4byte	.LVL51-.Ltext0
	.4byte	.LVL52-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST173:
	.4byte	.LVL836-.Ltext0
	.4byte	.LVL839-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL850-.Ltext0
	.4byte	.LVL852-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST174:
	.4byte	.LVL840-.Ltext0
	.4byte	.LVL850-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL852-.Ltext0
	.4byte	.LVL853-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST175:
	.4byte	.LVL838-.Ltext0
	.4byte	.LVL850-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL852-.Ltext0
	.4byte	.LVL853-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST176:
	.4byte	.LVL839-.Ltext0
	.4byte	.LVL846-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL847-.Ltext0
	.4byte	.LVL850-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL852-.Ltext0
	.4byte	.LVL853-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST111:
	.4byte	.LVL519-.Ltext0
	.4byte	.LVL521-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST112:
	.4byte	.LVL519-.Ltext0
	.4byte	.LVL520-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL520-.Ltext0
	.4byte	.LFE165-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST16:
	.4byte	.LVL43-.Ltext0
	.4byte	.LVL44-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL44-1-.Ltext0
	.4byte	.LFE163-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST17:
	.4byte	.LVL47-.Ltext0
	.4byte	.LVL49-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST18:
	.4byte	.LVL45-.Ltext0
	.4byte	.LVL48-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST14:
	.4byte	.LVL37-.Ltext0
	.4byte	.LVL38-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL38-1-.Ltext0
	.4byte	.LVL42-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST15:
	.4byte	.LVL39-.Ltext0
	.4byte	.LVL42-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST283:
	.4byte	.LVL1556-.Ltext0
	.4byte	.LVL1559-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST284:
	.4byte	.LVL1556-.Ltext0
	.4byte	.LVL1558-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST285:
	.4byte	.LVL1556-.Ltext0
	.4byte	.LVL1557-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST269:
	.4byte	.LVL1454-.Ltext0
	.4byte	.LVL1457-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST270:
	.4byte	.LVL1454-.Ltext0
	.4byte	.LVL1456-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST271:
	.4byte	.LVL1454-.Ltext0
	.4byte	.LVL1455-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST234:
	.4byte	.LVL1222-.Ltext0
	.4byte	.LVL1223-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1223-.Ltext0
	.4byte	.LVL1229-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST235:
	.4byte	.LVL1222-.Ltext0
	.4byte	.LVL1226-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1226-1-.Ltext0
	.4byte	.LFE158-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST236:
	.4byte	.LVL1224-.Ltext0
	.4byte	.LVL1225-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1230-.Ltext0
	.4byte	.LFE158-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST237:
	.4byte	.LVL1224-.Ltext0
	.4byte	.LVL1225-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1228-.Ltext0
	.4byte	.LFE158-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST238:
	.4byte	.LVL1224-.Ltext0
	.4byte	.LVL1225-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1231-.Ltext0
	.4byte	.LVL1236-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	.LVL1236-.Ltext0
	.4byte	.LVL1237-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1237-.Ltext0
	.4byte	.LFE158-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -48
	.4byte	0
	.4byte	0
.LLST239:
	.4byte	.LVL1224-.Ltext0
	.4byte	.LVL1225-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	.LVL1231-.Ltext0
	.4byte	.LFE158-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -44
	.4byte	0
	.4byte	0
.LLST272:
	.4byte	.LVL1459-.Ltext0
	.4byte	.LVL1461-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1463-.Ltext0
	.4byte	.LVL1466-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST273:
	.4byte	.LVL1459-.Ltext0
	.4byte	.LVL1462-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1462-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST274:
	.4byte	.LVL1459-.Ltext0
	.4byte	.LVL1460-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1463-.Ltext0
	.4byte	.LVL1464-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1464-.Ltext0
	.4byte	.LVL1465-.Ltext0
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
	.4byte	.LVL1524-.Ltext0
	.4byte	.LVL1525-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1536-.Ltext0
	.4byte	.LVL1537-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST275:
	.4byte	.LVL1459-.Ltext0
	.4byte	.LVL1462-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1463-.Ltext0
	.4byte	.LVL1464-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1509-.Ltext0
	.4byte	.LVL1510-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1526-.Ltext0
	.4byte	.LVL1527-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1528-.Ltext0
	.4byte	.LVL1530-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1545-.Ltext0
	.4byte	.LVL1546-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1548-.Ltext0
	.4byte	.LVL1549-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST276:
	.4byte	.LVL1462-.Ltext0
	.4byte	.LVL1463-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1475-.Ltext0
	.4byte	.LVL1501-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1511-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST277:
	.4byte	.LVL1462-.Ltext0
	.4byte	.LVL1463-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1468-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST278:
	.4byte	.LVL1469-.Ltext0
	.4byte	.LVL1472-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1494-.Ltext0
	.4byte	.LVL1495-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1521-.Ltext0
	.4byte	.LVL1522-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1529-.Ltext0
	.4byte	.LVL1530-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1542-.Ltext0
	.4byte	.LVL1543-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST279:
	.4byte	.LVL1462-.Ltext0
	.4byte	.LVL1463-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -72
	.4byte	.LVL1475-.Ltext0
	.4byte	.LVL1501-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -72
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -72
	.4byte	.LVL1511-.Ltext0
	.4byte	.LVL1532-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -72
	.4byte	.LVL1532-.Ltext0
	.4byte	.LVL1533-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1533-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -72
	.4byte	0
	.4byte	0
.LLST280:
	.4byte	.LVL1475-.Ltext0
	.4byte	.LVL1477-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1489-.Ltext0
	.4byte	.LVL1490-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1492-.Ltext0
	.4byte	.LVL1494-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1494-.Ltext0
	.4byte	.LVL1495-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1513-.Ltext0
	.4byte	.LVL1514-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1517-.Ltext0
	.4byte	.LVL1518-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1518-.Ltext0
	.4byte	.LVL1519-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1534-.Ltext0
	.4byte	.LVL1535-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1549-.Ltext0
	.4byte	.LVL1550-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1553-.Ltext0
	.4byte	.LVL1554-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1555-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST281:
	.4byte	.LVL1470-.Ltext0
	.4byte	.LVL1471-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1471-.Ltext0
	.4byte	.LVL1473-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1473-.Ltext0
	.4byte	.LVL1476-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1476-.Ltext0
	.4byte	.LVL1478-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1486-.Ltext0
	.4byte	.LVL1487-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1491-.Ltext0
	.4byte	.LVL1492-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1493-.Ltext0
	.4byte	.LVL1494-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1501-.Ltext0
	.4byte	.LVL1502-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1552-.Ltext0
	.4byte	.LVL1555-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1555-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST282:
	.4byte	.LVL1462-.Ltext0
	.4byte	.LVL1463-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1474-.Ltext0
	.4byte	.LVL1483-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1484-.Ltext0
	.4byte	.LVL1496-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1506-.Ltext0
	.4byte	.LVL1507-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1511-.Ltext0
	.4byte	.LFE157-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST227:
	.4byte	.LVL1191-.Ltext0
	.4byte	.LVL1195-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1195-1-.Ltext0
	.4byte	.LVL1196-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1201-.Ltext0
	.4byte	.LVL1202-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1212-.Ltext0
	.4byte	.LVL1215-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST228:
	.4byte	.LVL1191-.Ltext0
	.4byte	.LVL1195-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1195-1-.Ltext0
	.4byte	.LVL1215-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST229:
	.4byte	.LVL1191-.Ltext0
	.4byte	.LVL1193-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST230:
	.4byte	.LVL1191-.Ltext0
	.4byte	.LVL1194-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1194-.Ltext0
	.4byte	.LVL1215-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST231:
	.4byte	.LVL1192-.Ltext0
	.4byte	.LVL1215-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST232:
	.4byte	.LVL1195-.Ltext0
	.4byte	.LVL1196-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	.LVL1196-.Ltext0
	.4byte	.LVL1204-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1204-.Ltext0
	.4byte	.LFE155-.Ltext0
	.2byte	0x2
	.byte	0x71
	.sleb128 -4
	.4byte	0
	.4byte	0
.LLST233:
	.4byte	.LVL1195-.Ltext0
	.4byte	.LVL1196-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1201-.Ltext0
	.4byte	.LVL1202-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1206-.Ltext0
	.4byte	.LVL1207-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1208-.Ltext0
	.4byte	.LVL1209-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1211-.Ltext0
	.4byte	.LVL1213-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST254:
	.4byte	.LVL1387-.Ltext0
	.4byte	.LVL1389-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1392-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1396-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1453-.Ltext0
	.4byte	.LFE154-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST255:
	.4byte	.LVL1387-.Ltext0
	.4byte	.LVL1391-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	.LVL1453-.Ltext0
	.4byte	.LFE154-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST256:
	.4byte	.LVL1387-.Ltext0
	.4byte	.LVL1388-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1393-.Ltext0
	.4byte	.LVL1394-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1394-.Ltext0
	.4byte	.LVL1395-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1420-.Ltext0
	.4byte	.LVL1422-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST257:
	.4byte	.LVL1387-.Ltext0
	.4byte	.LVL1390-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1390-1-.Ltext0
	.4byte	.LFE154-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST258:
	.4byte	.LVL1401-.Ltext0
	.4byte	.LVL1403-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1417-.Ltext0
	.4byte	.LVL1418-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1437-.Ltext0
	.4byte	.LVL1438-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1441-.Ltext0
	.4byte	.LVL1442-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1445-.Ltext0
	.4byte	.LVL1446-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1449-.Ltext0
	.4byte	.LVL1450-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST259:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1392-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1404-.Ltext0
	.4byte	.LVL1410-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1412-.Ltext0
	.4byte	.LVL1413-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1423-.Ltext0
	.4byte	.LVL1424-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1439-.Ltext0
	.4byte	.LVL1440-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1441-.Ltext0
	.4byte	.LVL1442-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1446-.Ltext0
	.4byte	.LVL1447-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1448-.Ltext0
	.4byte	.LVL1449-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1450-.Ltext0
	.4byte	.LVL1451-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1452-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST260:
	.4byte	.LVL1426-.Ltext0
	.4byte	.LVL1427-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1428-.Ltext0
	.4byte	.LVL1429-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST261:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1402-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST262:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1407-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	0
	.4byte	0
.LLST263:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1407-.Ltext0
	.4byte	.LVL1415-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1433-.Ltext0
	.4byte	.LVL1445-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1449-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST264:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1398-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST265:
	.4byte	.LVL1405-.Ltext0
	.4byte	.LVL1406-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1406-.Ltext0
	.4byte	.LVL1408-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1408-.Ltext0
	.4byte	.LVL1409-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1412-.Ltext0
	.4byte	.LVL1414-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1430-.Ltext0
	.4byte	.LVL1431-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1432-.Ltext0
	.4byte	.LVL1434-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST266:
	.4byte	.LVL1420-.Ltext0
	.4byte	.LVL1421-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1435-.Ltext0
	.4byte	.LVL1436-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST267:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1397-.Ltext0
	.4byte	.LVL1453-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST268:
	.4byte	.LVL1391-.Ltext0
	.4byte	.LVL1393-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1407-.Ltext0
	.4byte	.LVL1416-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	.LVL1425-.Ltext0
	.4byte	.LVL1433-.Ltext0
	.2byte	0x1
	.byte	0x5a
	.4byte	0
	.4byte	0
.LLST288:
	.4byte	.LVL1611-.Ltext0
	.4byte	.LVL1614-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1628-.Ltext0
	.4byte	.LVL1630-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	.LVL1631-.Ltext0
	.4byte	.LVL1632-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST289:
	.4byte	.LVL1609-.Ltext0
	.4byte	.LVL1610-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1619-.Ltext0
	.4byte	.LVL1621-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1622-.Ltext0
	.4byte	.LVL1626-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST96:
	.4byte	.LVL410-.Ltext0
	.4byte	.LVL412-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL431-.Ltext0
	.4byte	.LVL434-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL434-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST97:
	.4byte	.LVL410-.Ltext0
	.4byte	.LVL432-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL434-.Ltext0
	.4byte	.LFE149-.Ltext0
	.2byte	0x1
	.byte	0x56
	.4byte	0
	.4byte	0
.LLST98:
	.4byte	.LVL411-.Ltext0
	.4byte	.LVL439-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL440-.Ltext0
	.4byte	.LVL441-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL442-.Ltext0
	.4byte	.LVL451-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL452-.Ltext0
	.4byte	.LFE149-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST99:
	.4byte	.LVL414-.Ltext0
	.4byte	.LVL415-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL416-.Ltext0
	.4byte	.LVL417-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL420-.Ltext0
	.4byte	.LVL421-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL429-.Ltext0
	.4byte	.LVL430-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST100:
	.4byte	.LVL419-.Ltext0
	.4byte	.LVL421-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL421-.Ltext0
	.4byte	.LVL422-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL424-.Ltext0
	.4byte	.LVL426-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL427-.Ltext0
	.4byte	.LVL428-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL428-.Ltext0
	.4byte	.LVL433-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL434-.Ltext0
	.4byte	.LVL435-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL444-.Ltext0
	.4byte	.LVL445-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL449-.Ltext0
	.4byte	.LVL450-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL454-.Ltext0
	.4byte	.LVL455-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL456-.Ltext0
	.4byte	.LFE149-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST101:
	.4byte	.LVL415-.Ltext0
	.4byte	.LVL416-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL417-.Ltext0
	.4byte	.LVL418-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL425-.Ltext0
	.4byte	.LVL428-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST102:
	.4byte	.LVL418-.Ltext0
	.4byte	.LVL419-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST110:
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
	.4byte	.LVL23-.Ltext0
	.4byte	.LVL24-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL24-.Ltext0
	.4byte	.LVL29-.Ltext0
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
	.4byte	.LFE142-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST63:
	.4byte	.LVL281-.Ltext0
	.4byte	.LVL282-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL282-1-.Ltext0
	.4byte	.LFE141-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST60:
	.4byte	.LVL202-.Ltext0
	.4byte	.LVL203-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL203-1-.Ltext0
	.4byte	.LFE140-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST61:
	.4byte	.LVL274-.Ltext0
	.4byte	.LVL275-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL276-.Ltext0
	.4byte	.LVL277-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST62:
	.4byte	.LVL270-.Ltext0
	.4byte	.LVL271-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL278-.Ltext0
	.4byte	.LVL279-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL280-.Ltext0
	.4byte	.LFE140-.Ltext0
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
	.4byte	.LVL35-.Ltext0
	.4byte	.LVL36-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST36:
	.4byte	.LVL113-.Ltext0
	.4byte	.LVL114-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST39:
	.4byte	.LVL125-.Ltext0
	.4byte	.LVL126-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST48:
	.4byte	.LVL157-.Ltext0
	.4byte	.LVL164-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL164-.Ltext0
	.4byte	.LFE192-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST49:
	.4byte	.LVL161-.Ltext0
	.4byte	.LVL162-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL164-.Ltext0
	.4byte	.LFE192-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST50:
	.4byte	.LVL165-.Ltext0
	.4byte	.LVL168-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL168-1-.Ltext0
	.4byte	.LFE195-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST51:
	.4byte	.LVL170-.Ltext0
	.4byte	.LVL171-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST52:
	.4byte	.LVL172-.Ltext0
	.4byte	.LVL173-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL174-.Ltext0
	.4byte	.LVL175-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST53:
	.4byte	.LVL172-.Ltext0
	.4byte	.LVL173-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL174-.Ltext0
	.4byte	.LVL176-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST68:
	.4byte	.LVL303-.Ltext0
	.4byte	.LVL304-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL304-.Ltext0
	.4byte	.LVL305-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST103:
	.4byte	.LVL457-.Ltext0
	.4byte	.LVL458-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL459-.Ltext0
	.4byte	.LVL460-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL464-.Ltext0
	.4byte	.LVL465-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST107:
	.4byte	.LVL477-.Ltext0
	.4byte	.LVL478-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL478-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST108:
	.4byte	.LVL477-.Ltext0
	.4byte	.LVL478-1-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL478-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST109:
	.4byte	.LVL477-.Ltext0
	.4byte	.LVL478-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL478-.Ltext0
	.4byte	.LFE267-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST141:
	.4byte	.LVL640-.Ltext0
	.4byte	.LVL645-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -112
	.4byte	0
	.4byte	0
.LLST182:
	.4byte	.LVL889-.Ltext0
	.4byte	.LVL891-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL892-.Ltext0
	.4byte	.LVL893-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL895-.Ltext0
	.4byte	.LVL896-1-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	0
	.4byte	0
.LLST183:
	.4byte	.LVL890-.Ltext0
	.4byte	.LVL900-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	.LVL901-.Ltext0
	.4byte	.LFE257-.Ltext0
	.2byte	0x1
	.byte	0x54
	.4byte	0
	.4byte	0
.LLST184:
	.4byte	.LVL902-.Ltext0
	.4byte	.LVL908-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL908-.Ltext0
	.4byte	.LFE241-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST186:
	.4byte	.LVL915-.Ltext0
	.4byte	.LVL925-.Ltext0
	.2byte	0x1
	.byte	0x57
	.4byte	0
	.4byte	0
.LLST187:
	.4byte	.LVL916-.Ltext0
	.4byte	.LVL925-.Ltext0
	.2byte	0x3
	.byte	0x91
	.sleb128 -72
	.4byte	0
	.4byte	0
.LLST188:
	.4byte	.LVL918-.Ltext0
	.4byte	.LVL919-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL921-.Ltext0
	.4byte	.LVL925-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST245:
	.4byte	.LVL1296-.Ltext0
	.4byte	.LVL1297-.Ltext0
	.2byte	0x1
	.byte	0x50
	.4byte	.LVL1297-.Ltext0
	.4byte	.LVL1305-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1311-.Ltext0
	.4byte	.LVL1313-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST246:
	.4byte	.LVL1296-.Ltext0
	.4byte	.LVL1298-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1311-.Ltext0
	.4byte	.LVL1312-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST247:
	.4byte	.LVL1306-.Ltext0
	.4byte	.LVL1307-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1308-.Ltext0
	.4byte	.LVL1312-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1313-.Ltext0
	.4byte	.LVL1314-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1315-.Ltext0
	.4byte	.LVL1319-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1320-.Ltext0
	.4byte	.LVL1321-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1325-.Ltext0
	.4byte	.LVL1337-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1338-.Ltext0
	.4byte	.LVL1367-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	.LVL1380-.Ltext0
	.4byte	.LVL1384-.Ltext0
	.2byte	0x1
	.byte	0x55
	.4byte	0
	.4byte	0
.LLST248:
	.4byte	.LVL1338-.Ltext0
	.4byte	.LVL1339-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1354-.Ltext0
	.4byte	.LVL1359-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	.LVL1365-.Ltext0
	.4byte	.LVL1366-.Ltext0
	.2byte	0x1
	.byte	0x58
	.4byte	0
	.4byte	0
.LLST249:
	.4byte	.LVL1311-.Ltext0
	.4byte	.LVL1312-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1318-.Ltext0
	.4byte	.LVL1319-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1325-.Ltext0
	.4byte	.LVL1361-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	.LVL1361-.Ltext0
	.4byte	.LVL1362-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1362-.Ltext0
	.4byte	.LFE259-.Ltext0
	.2byte	0x2
	.byte	0x91
	.sleb128 -52
	.4byte	0
	.4byte	0
.LLST250:
	.4byte	.LVL1338-.Ltext0
	.4byte	.LVL1340-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1344-.Ltext0
	.4byte	.LVL1346-.Ltext0
	.2byte	0x1
	.byte	0x5e
	.4byte	.LVL1347-.Ltext0
	.4byte	.LVL1348-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1349-.Ltext0
	.4byte	.LVL1352-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1355-.Ltext0
	.4byte	.LVL1356-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1366-.Ltext0
	.4byte	.LVL1369-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1373-1-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1374-.Ltext0
	.4byte	.LVL1375-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1379-.Ltext0
	.4byte	.LVL1381-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1384-.Ltext0
	.4byte	.LVL1385-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	0
	.4byte	0
.LLST251:
	.4byte	.LVL1343-.Ltext0
	.4byte	.LVL1345-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1350-.Ltext0
	.4byte	.LVL1352-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	0
	.4byte	0
.LLST252:
	.4byte	.LVL1334-.Ltext0
	.4byte	.LVL1335-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1336-.Ltext0
	.4byte	.LVL1342-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1351-.Ltext0
	.4byte	.LVL1352-.Ltext0
	.2byte	0x1
	.byte	0x51
	.4byte	.LVL1366-.Ltext0
	.4byte	.LVL1368-.Ltext0
	.2byte	0x1
	.byte	0x52
	.4byte	.LVL1368-.Ltext0
	.4byte	.LVL1370-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1371-.Ltext0
	.4byte	.LVL1372-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	0
	.4byte	0
.LLST253:
	.4byte	.LVL1311-.Ltext0
	.4byte	.LVL1312-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1332-.Ltext0
	.4byte	.LVL1341-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1354-.Ltext0
	.4byte	.LVL1382-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	.LVL1384-.Ltext0
	.4byte	.LFE259-.Ltext0
	.2byte	0x1
	.byte	0x59
	.4byte	0
	.4byte	0
.LLST287:
	.4byte	.LVL1591-.Ltext0
	.4byte	.LVL1592-1-.Ltext0
	.2byte	0x1
	.byte	0x53
	.4byte	.LVL1599-.Ltext0
	.4byte	.LVL1600-.Ltext0
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
	.4byte	.LBB234-.Ltext0
	.4byte	.LBE234-.Ltext0
	.4byte	.LBB235-.Ltext0
	.4byte	.LBE235-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB241-.Ltext0
	.4byte	.LBE241-.Ltext0
	.4byte	.LBB248-.Ltext0
	.4byte	.LBE248-.Ltext0
	.4byte	.LBB249-.Ltext0
	.4byte	.LBE249-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB254-.Ltext0
	.4byte	.LBE254-.Ltext0
	.4byte	.LBB255-.Ltext0
	.4byte	.LBE255-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB274-.Ltext0
	.4byte	.LBE274-.Ltext0
	.4byte	.LBB277-.Ltext0
	.4byte	.LBE277-.Ltext0
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
	.4byte	.LBB353-.Ltext0
	.4byte	.LBE353-.Ltext0
	.4byte	.LBB354-.Ltext0
	.4byte	.LBE354-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB362-.Ltext0
	.4byte	.LBE362-.Ltext0
	.4byte	.LBB367-.Ltext0
	.4byte	.LBE367-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB368-.Ltext0
	.4byte	.LBE368-.Ltext0
	.4byte	.LBB369-.Ltext0
	.4byte	.LBE369-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB376-.Ltext0
	.4byte	.LBE376-.Ltext0
	.4byte	.LBB378-.Ltext0
	.4byte	.LBE378-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB420-.Ltext0
	.4byte	.LBE420-.Ltext0
	.4byte	.LBB427-.Ltext0
	.4byte	.LBE427-.Ltext0
	.4byte	.LBB428-.Ltext0
	.4byte	.LBE428-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB429-.Ltext0
	.4byte	.LBE429-.Ltext0
	.4byte	.LBB432-.Ltext0
	.4byte	.LBE432-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB435-.Ltext0
	.4byte	.LBE435-.Ltext0
	.4byte	.LBB438-.Ltext0
	.4byte	.LBE438-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB447-.Ltext0
	.4byte	.LBE447-.Ltext0
	.4byte	.LBB448-.Ltext0
	.4byte	.LBE448-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB451-.Ltext0
	.4byte	.LBE451-.Ltext0
	.4byte	.LBB452-.Ltext0
	.4byte	.LBE452-.Ltext0
	.4byte	.LBB453-.Ltext0
	.4byte	.LBE453-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB465-.Ltext0
	.4byte	.LBE465-.Ltext0
	.4byte	.LBB470-.Ltext0
	.4byte	.LBE470-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB497-.Ltext0
	.4byte	.LBE497-.Ltext0
	.4byte	.LBB504-.Ltext0
	.4byte	.LBE504-.Ltext0
	.4byte	.LBB505-.Ltext0
	.4byte	.LBE505-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB499-.Ltext0
	.4byte	.LBE499-.Ltext0
	.4byte	.LBB500-.Ltext0
	.4byte	.LBE500-.Ltext0
	.4byte	.LBB501-.Ltext0
	.4byte	.LBE501-.Ltext0
	.4byte	0
	.4byte	0
	.4byte	.LBB522-.Ltext0
	.4byte	.LBE522-.Ltext0
	.4byte	.LBB523-.Ltext0
	.4byte	.LBE523-.Ltext0
	.4byte	0
	.4byte	0
	.section	.debug_line,"",%progbits
.Ldebug_line0:
	.section	.debug_str,"MS",%progbits,1
.LASF471:
	.ascii	"recovery_flash_mode\000"
.LASF47:
	.ascii	"root_mountflags\000"
.LASF401:
	.ascii	"error_phy_page\000"
.LASF339:
	.ascii	"gc_ink_free_return_value\000"
.LASF172:
	.ascii	"available_pages\000"
.LASF467:
	.ascii	"mlc_detected_active_page\000"
.LASF389:
	.ascii	"spperBlk\000"
.LASF14:
	.ascii	"size_t\000"
.LASF190:
	.ascii	"c_ftl_nand_planes_num\000"
.LASF1:
	.ascii	"sizetype\000"
.LASF594:
	.ascii	"re_try_next_blk\000"
.LASF24:
	.ascii	"saved_command_line\000"
.LASF589:
	.ascii	"P2V_block_in_plane\000"
.LASF344:
	.ascii	"ftl_memcmp\000"
.LASF217:
	.ascii	"g_GlobalSysVersion\000"
.LASF265:
	.ascii	"g_totle_vendor_block\000"
.LASF97:
	.ascii	"BlkNum\000"
.LASF599:
	.ascii	"totle_count\000"
.LASF489:
	.ascii	"lastWritePage\000"
.LASF446:
	.ascii	"ftl_check_vpc\000"
.LASF356:
	.ascii	"request\000"
.LASF327:
	.ascii	"DeviceCapacity\000"
.LASF559:
	.ascii	"List_remove_node\000"
.LASF136:
	.ascii	"PowerOnTimes\000"
.LASF499:
	.ascii	"FtlLoadEctTbl\000"
.LASF386:
	.ascii	"req_read_temp\000"
.LASF70:
	.ascii	"plane_per_die\000"
.LASF59:
	.ascii	"uint32\000"
.LASF450:
	.ascii	"FtlSuperblockPowerLostFix\000"
.LASF226:
	.ascii	"gL2pMapInfo\000"
.LASF502:
	.ascii	"pMapHeader\000"
.LASF363:
	.ascii	"__func__\000"
.LASF478:
	.ascii	"prev_ppa_ver\000"
.LASF87:
	.ascii	"prev_id\000"
.LASF104:
	.ascii	"dump_write_en\000"
.LASF149:
	.ascii	"max_erase_count\000"
.LASF229:
	.ascii	"req_read\000"
.LASF396:
	.ascii	"FtlGcScanTempBlk\000"
.LASF405:
	.ascii	"BOPS_EN\000"
.LASF422:
	.ascii	"get_new_active_ppa\000"
.LASF324:
	.ascii	"g_nand_phy_info\000"
.LASF404:
	.ascii	"FtlGcFreeTempBlock\000"
.LASF252:
	.ascii	"g_totle_swl_count\000"
.LASF564:
	.ascii	"tmp_id\000"
.LASF95:
	.ascii	"sign\000"
.LASF233:
	.ascii	"req_gc_dst\000"
.LASF443:
	.ascii	"FtlSysBlkInit\000"
.LASF468:
	.ascii	"lookup_superblock_id\000"
.LASF142:
	.ascii	"gc_page_count\000"
.LASF567:
	.ascii	"tmp_data_count\000"
.LASF191:
	.ascii	"c_ftl_nand_blk_pre_plane\000"
.LASF76:
	.ascii	"sec_per_blk\000"
.LASF371:
	.ascii	"sftl_gc\000"
.LASF383:
	.ascii	"get_new_gc_superblock\000"
.LASF563:
	.ascii	"tmp_erase_count\000"
.LASF425:
	.ascii	"allocate_new_data_superblock\000"
.LASF382:
	.ascii	"gc_next_superblock\000"
.LASF23:
	.ascii	"boot_command_line\000"
.LASF510:
	.ascii	"FtlUpdateVaildLpn\000"
.LASF534:
	.ascii	"update_map_block\000"
.LASF608:
	.ascii	"FtlBbmMapBadBlock\000"
.LASF606:
	.ascii	"FtlBbtInfoPrint\000"
.LASF56:
	.ascii	"hex_asc_upper\000"
.LASF462:
	.ascii	"saved_active_page\000"
.LASF506:
	.ascii	"re_save_vpndata\000"
.LASF536:
	.ascii	"region_num\000"
.LASF603:
	.ascii	"re_save_bbmdata\000"
.LASF623:
	.ascii	"first_lpa_sctidx\000"
.LASF21:
	.ascii	"__security_initcall_start\000"
.LASF308:
	.ascii	"g_totle_slc_erase_count\000"
.LASF597:
	.ascii	"FtlLoadBbt\000"
.LASF176:
	.ascii	"check_en\000"
.LASF222:
	.ascii	"g_MaxLbn\000"
.LASF63:
	.ascii	"page_addr\000"
.LASF398:
	.ascii	"totle_num\000"
.LASF325:
	.ascii	"g_nand_ops\000"
.LASF588:
	.ascii	"ver2\000"
.LASF215:
	.ascii	"c_ftl_nand_max_data_blks\000"
.LASF214:
	.ascii	"c_ftl_nand_data_op_blks_per_plane\000"
.LASF571:
	.ascii	"FtlFreeSysBlkQueueOut\000"
.LASF117:
	.ascii	"ftl_data_blk_header\000"
.LASF231:
	.ascii	"req_erase\000"
.LASF615:
	.ascii	"num_lpa\000"
.LASF200:
	.ascii	"c_ftl_nand_byte_pre_oob\000"
.LASF511:
	.ascii	"FtlEctTblFlush\000"
.LASF121:
	.ascii	"ftl_sys_blk_header\000"
.LASF345:
	.ascii	"ftl_memcpy\000"
.LASF301:
	.ascii	"g_totle_read_page_count\000"
.LASF237:
	.ascii	"p_sys_data_buf_1\000"
.LASF45:
	.ascii	"sysctl_panic_on_stackoverflow\000"
.LASF203:
	.ascii	"c_ftl_nand_map_region_num\000"
.LASF53:
	.ascii	"SYSTEM_RESTART\000"
.LASF126:
	.ascii	"activeFlashMode\000"
.LASF364:
	.ascii	"read_req\000"
.LASF261:
	.ascii	"p_vendor_block_table\000"
.LASF148:
	.ascii	"discard_page_count\000"
.LASF544:
	.ascii	"vpcTbl\000"
.LASF276:
	.ascii	"g_buffer_superblock\000"
.LASF10:
	.ascii	"long int\000"
.LASF636:
	.ascii	"create_first_buffer_superblock\000"
.LASF64:
	.ascii	"p_data\000"
.LASF246:
	.ascii	"c_gc_page_buf_num\000"
.LASF655:
	.ascii	"limit\000"
.LASF515:
	.ascii	"Index\000"
.LASF225:
	.ascii	"gSysFreeQueue\000"
.LASF145:
	.ascii	"l2p_write_count\000"
.LASF622:
	.ascii	"absolute_sector\000"
.LASF557:
	.ascii	"pHead\000"
.LASF67:
	.ascii	"nand_phy_info\000"
.LASF185:
	.ascii	"c_ftl_nand_type\000"
.LASF335:
	.ascii	"FtlUpdateVaildLpnCount\000"
.LASF352:
	.ascii	"FlashTestBlk\000"
.LASF673:
	.ascii	"__builtin_strcpy\000"
.LASF291:
	.ascii	"g_gc_bad_block_gc_index\000"
.LASF270:
	.ascii	"p_free_data_block_list_head\000"
.LASF101:
	.ascii	"pBlkVpcTbl\000"
.LASF583:
	.ascii	"high\000"
.LASF638:
	.ascii	"is_sys_blk\000"
.LASF264:
	.ascii	"p_vendor_region_ppn_table\000"
.LASF546:
	.ascii	"load_l2p_region\000"
.LASF532:
	.ascii	"flush_l2p_region\000"
.LASF38:
	.ascii	"panic_blink\000"
.LASF458:
	.ascii	"num_free_node\000"
.LASF360:
	.ascii	"block_size\000"
.LASF347:
	.ascii	"pvForm\000"
.LASF209:
	.ascii	"c_ftl_vendor_part_size\000"
.LASF560:
	.ascii	"pTmp\000"
.LASF390:
	.ascii	"FtlGcMarkBadPhyBlk\000"
.LASF114:
	.ascii	"ftl_bbt_blk_header\000"
.LASF444:
	.ascii	"ftl_scan_all_data\000"
.LASF354:
	.ascii	"spare\000"
.LASF570:
	.ascii	"INSERT_FREE_LIST\000"
.LASF55:
	.ascii	"hex_asc\000"
.LASF4:
	.ascii	"signed char\000"
.LASF299:
	.ascii	"g_totle_read_sector\000"
.LASF66:
	.ascii	"nand_req\000"
.LASF263:
	.ascii	"p_vendor_block_ver_table\000"
.LASF323:
	.ascii	"gNandPhyInfo\000"
.LASF484:
	.ascii	"FtlL2PDataInit\000"
.LASF523:
	.ascii	"action\000"
.LASF406:
	.ascii	"superblock_id\000"
.LASF314:
	.ascii	"g_gc_head_data_block\000"
.LASF54:
	.ascii	"system_state\000"
.LASF621:
	.ascii	"FtlRead\000"
.LASF273:
	.ascii	"g_num_free_superblocks\000"
.LASF593:
	.ascii	"pBbtHeader\000"
.LASF5:
	.ascii	"unsigned char\000"
.LASF346:
	.ascii	"pvTo\000"
.LASF48:
	.ascii	"early_boot_irqs_disabled\000"
.LASF205:
	.ascii	"c_ftl_nand_vendor_region_num\000"
.LASF464:
	.ascii	"next_free_active_plane\000"
.LASF521:
	.ascii	"FtlVendorPartWrite\000"
.LASF49:
	.ascii	"SYSTEM_BOOTING\000"
.LASF120:
	.ascii	"regionId\000"
.LASF613:
	.ascii	"start_lpa\000"
.LASF311:
	.ascii	"g_in_gc_progress\000"
.LASF19:
	.ascii	"__con_initcall_start\000"
.LASF84:
	.ascii	"read_page\000"
.LASF665:
	.ascii	"GNU C89 6.3.1 20170404 -mlittle-endian -mapcs -mno-"
	.ascii	"sched-prolog -mabi=aapcs-linux -mno-thumb-interwork"
	.ascii	" -mfpu=vfp -marm -march=armv7-a -mfloat-abi=soft -m"
	.ascii	"tune=cortex-a9 -mtls-dialect=gnu -g -Os -std=gnu90 "
	.ascii	"-p -fno-strict-aliasing -fno-common -fno-PIE -fno-d"
	.ascii	"warf2-cfi-asm -fno-ipa-sra -funwind-tables -fno-del"
	.ascii	"ete-null-pointer-checks -fno-stack-protector -fno-o"
	.ascii	"mit-frame-pointer -fno-optimize-sibling-calls -fno-"
	.ascii	"var-tracking-assignments -fno-strict-overflow -fno-"
	.ascii	"merge-all-constants -fmerge-constants -fstack-check"
	.ascii	"=no -fconserve-stack --param allow-store-data-races"
	.ascii	"=0\000"
.LASF432:
	.ascii	"GetSwlReplaceBlock\000"
.LASF91:
	.ascii	"ftl_sys_info\000"
.LASF159:
	.ascii	"refresh_start_lpa\000"
.LASF460:
	.ascii	"FtlPowerLostRecovery\000"
.LASF561:
	.ascii	"insert_free_list\000"
.LASF13:
	.ascii	"_Bool\000"
.LASF461:
	.ascii	"FtlRecoverySuperblock\000"
.LASF29:
	.ascii	"linux_proc_banner\000"
.LASF430:
	.ascii	"get_new_id\000"
.LASF317:
	.ascii	"g_cur_erase_blk\000"
.LASF350:
	.ascii	"table\000"
.LASF166:
	.ascii	"value\000"
.LASF224:
	.ascii	"gSysInfo\000"
.LASF375:
	.ascii	"gc_page_num\000"
.LASF349:
	.ascii	"FlashGetBadBlockList\000"
.LASF90:
	.ascii	"BbtMap\000"
.LASF2:
	.ascii	"char\000"
.LASF650:
	.ascii	"FtlSysBlkNumInit\000"
.LASF659:
	.ascii	"memset\000"
.LASF329:
	.ascii	"TotleReadBufferCount1\000"
.LASF161:
	.ascii	"totle_read_sector\000"
.LASF331:
	.ascii	"TotleReadBufferCount3\000"
.LASF628:
	.ascii	"FtlSysFlush\000"
.LASF258:
	.ascii	"p_map_block_ver_table\000"
.LASF132:
	.ascii	"bufferPageOffset\000"
.LASF525:
	.ascii	"l2p_region_id\000"
.LASF411:
	.ascii	"FtlGcUpdatePage\000"
.LASF437:
	.ascii	"maxFreeBlockEraseCount\000"
.LASF643:
	.ascii	"re_test_next_page\000"
.LASF43:
	.ascii	"panic_on_io_nmi\000"
.LASF426:
	.ascii	"allocate_data_superblock\000"
.LASF576:
	.ascii	"FtlFreeSysBlkQueueIn\000"
.LASF492:
	.ascii	"pMapBlkHeader\000"
.LASF72:
	.ascii	"blk_per_die\000"
.LASF330:
	.ascii	"TotleReadBufferCount2\000"
.LASF378:
	.ascii	"num_io\000"
.LASF585:
	.ascii	"pagePreBlk\000"
.LASF130:
	.ascii	"activePageOffset\000"
.LASF78:
	.ascii	"reserved_blk\000"
.LASF483:
	.ascii	"FtlLoadMapInfo\000"
.LASF367:
	.ascii	"block_index\000"
.LASF652:
	.ascii	"FtlPrintInfo\000"
.LASF481:
	.ascii	"FtlReUsePrevPpa\000"
.LASF251:
	.ascii	"p_swl_mul_table\000"
.LASF565:
	.ascii	"insert_data_list\000"
.LASF646:
	.ascii	"FtlVariablesInit\000"
.LASF269:
	.ascii	"p_data_block_list_table\000"
.LASF513:
	.ascii	"ec_mod_count\000"
.LASF436:
	.ascii	"pNode\000"
.LASF305:
	.ascii	"g_totle_mlc_erase_count\000"
.LASF586:
	.ascii	"ftl_cmp_data_ver\000"
.LASF277:
	.ascii	"g_gc_temp_superblock\000"
.LASF262:
	.ascii	"p_vendor_block_valid_page_count\000"
.LASF500:
	.ascii	"FtlScanSysBlk\000"
.LASF669:
	.ascii	"l2p_addr_tran\000"
.LASF310:
	.ascii	"g_max_erase_count\000"
.LASF211:
	.ascii	"c_ftl_nand_init_sys_blks_per_plane\000"
.LASF183:
	.ascii	"ftl_gc_page_buffer\000"
.LASF234:
	.ascii	"g_req_cache\000"
.LASF649:
	.ascii	"FtlConstantsInit\000"
.LASF452:
	.ascii	"pages\000"
.LASF421:
	.ascii	"update_vpc_list\000"
.LASF662:
	.ascii	"rknand_print_hex\000"
.LASF409:
	.ascii	"p_gc_page\000"
.LASF15:
	.ascii	"elf_hwcap\000"
.LASF395:
	.ascii	"Ftl_get_new_temp_ppa\000"
.LASF137:
	.ascii	"PowerLostTimes\000"
.LASF578:
	.ascii	"blk_Id\000"
.LASF294:
	.ascii	"g_gc_merge_free_blk_threshold\000"
.LASF39:
	.ascii	"oops_in_progress\000"
.LASF433:
	.ascii	"node_id\000"
.LASF135:
	.ascii	"cache_write_count\000"
.LASF260:
	.ascii	"g_totle_map_block\000"
.LASF660:
	.ascii	"printk\000"
.LASF547:
	.ascii	"phyAddr\000"
.LASF187:
	.ascii	"c_ftl_nand_planes_per_die\000"
.LASF490:
	.ascii	"ppnTbl\000"
.LASF365:
	.ascii	"FlashReadPages\000"
.LASF376:
	.ascii	"gc_blk\000"
.LASF485:
	.ascii	"FtlLoadVonderInfo\000"
.LASF555:
	.ascii	"List_get_gc_head_node\000"
.LASF504:
	.ascii	"pSysHeader\000"
.LASF181:
	.ascii	"scr_ppa\000"
.LASF210:
	.ascii	"c_ftl_nand_sys_blks_per_plane\000"
.LASF573:
	.ascii	"FtlFreeSysBLkSort\000"
.LASF267:
	.ascii	"p_l2p_map_buf\000"
.LASF644:
	.ascii	"IsBlkInVendorPart\000"
.LASF600:
	.ascii	"FtlBbtMemInit\000"
.LASF245:
	.ascii	"gp_gc_page_buf_info\000"
.LASF574:
	.ascii	"tmp_blk_id\000"
.LASF168:
	.ascii	"front\000"
.LASF236:
	.ascii	"p_sys_data_buf\000"
.LASF94:
	.ascii	"index\000"
.LASF539:
	.ascii	"lastMapBlk\000"
.LASF625:
	.ascii	"last_lpa_nscts\000"
.LASF302:
	.ascii	"g_tmp_data_superblock_id\000"
.LASF248:
	.ascii	"gp_ect_tbl_info\000"
.LASF656:
	.ascii	"rknand_proc_ftlread\000"
.LASF543:
	.ascii	"ftl_free_no_use_map_blk\000"
.LASF667:
	.ascii	"/home/ldq/rk-linux/kernel-debug\000"
.LASF36:
	.ascii	"atomic_notifier_head\000"
.LASF385:
	.ascii	"FtlReadRefresh\000"
.LASF188:
	.ascii	"c_ftl_nand_blks_per_die\000"
.LASF566:
	.ascii	"node_data_count\000"
.LASF602:
	.ascii	"write_count\000"
.LASF242:
	.ascii	"p_io_spare_buf\000"
.LASF480:
	.ascii	"current_ppa_ver\000"
.LASF0:
	.ascii	"long unsigned int\000"
.LASF174:
	.ascii	"num_planes\000"
.LASF122:
	.ascii	"res32_0\000"
.LASF123:
	.ascii	"res32_1\000"
.LASF434:
	.ascii	"min_ec_id\000"
.LASF362:
	.ascii	"check\000"
.LASF30:
	.ascii	"console_printk\000"
.LASF672:
	.ascii	"strcpy\000"
.LASF62:
	.ascii	"status\000"
.LASF69:
	.ascii	"die_num\000"
.LASF629:
	.ascii	"sftl_init\000"
.LASF293:
	.ascii	"g_gc_free_blk_threshold\000"
.LASF33:
	.ascii	"kptr_restrict\000"
.LASF595:
	.ascii	"FtlLoadFactoryBbt\000"
.LASF408:
	.ascii	"scan_page_num\000"
.LASF150:
	.ascii	"min_erase_count\000"
.LASF186:
	.ascii	"c_ftl_nand_die_num\000"
.LASF307:
	.ascii	"g_min_erase_count\000"
.LASF482:
	.ascii	"ftl_sb_update_avl_pages\000"
.LASF106:
	.ascii	"region_id\000"
.LASF632:
	.ascii	"ftl_low_format\000"
.LASF343:
	.ascii	"str2\000"
.LASF96:
	.ascii	"maxRegion\000"
.LASF326:
	.ascii	"ftl_temp_buf\000"
.LASF162:
	.ascii	"progErrorCount\000"
.LASF524:
	.ascii	"ram_region_id\000"
.LASF336:
	.ascii	"check_vpc_table\000"
.LASF454:
	.ascii	"FtlGcPageRecovery\000"
.LASF31:
	.ascii	"printk_delay_msec\000"
.LASF494:
	.ascii	"ftl_get_blk_mode\000"
.LASF28:
	.ascii	"linux_banner\000"
.LASF664:
	.ascii	"sprintf\000"
.LASF109:
	.ascii	"ftl_ect_tbl_info\000"
.LASF541:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
.LASF514:
	.ascii	"FtlVendorPartRead\000"
.LASF612:
	.ascii	"FtlWrite\000"
.LASF75:
	.ascii	"sec_per_page\000"
.LASF71:
	.ascii	"blk_per_plane\000"
.LASF402:
	.ascii	"ReInit\000"
.LASF519:
	.ascii	"result\000"
.LASF100:
	.ascii	"pBlkVerTbl\000"
.LASF451:
	.ascii	"req_temp\000"
.LASF118:
	.ascii	"lastPpa\000"
.LASF322:
	.ascii	"g_power_lost_recovery_flag\000"
.LASF604:
	.ascii	"FtlBbt2Bitmap\000"
.LASF73:
	.ascii	"page_per_blk\000"
.LASF473:
	.ascii	"recovery_super_page\000"
.LASF313:
	.ascii	"g_ftl_nand_free_count\000"
.LASF369:
	.ascii	"die_index\000"
.LASF416:
	.ascii	"numREQ\000"
.LASF79:
	.ascii	"ecc_bits\000"
.LASF634:
	.ascii	"sys_bad_block_cnt\000"
.LASF196:
	.ascii	"c_ftl_nand_page_pre_super_blk\000"
.LASF178:
	.ascii	"reversed\000"
.LASF403:
	.ascii	"read_super_page\000"
.LASF40:
	.ascii	"panic_timeout\000"
.LASF128:
	.ascii	"gcTempFlashMode\000"
.LASF296:
	.ascii	"g_totle_gc_page_count\000"
.LASF238:
	.ascii	"p_vendor_data_buf\000"
.LASF601:
	.ascii	"FtlBbmTblFlush\000"
.LASF568:
	.ascii	"tmp_multiplier_value\000"
.LASF661:
	.ascii	"__memzero\000"
.LASF407:
	.ascii	"lookup_ppa\000"
.LASF8:
	.ascii	"long long int\000"
.LASF306:
	.ascii	"g_totle_avg_erase_count\000"
.LASF216:
	.ascii	"ftl_gc_temp_power_lost_recovery_flag\000"
.LASF552:
	.ascii	"prev_valid_page_count\000"
.LASF388:
	.ascii	"FtlGcFreeBadSuperBlk\000"
.LASF11:
	.ascii	"__kernel_size_t\000"
.LASF223:
	.ascii	"gBbtInfo\000"
.LASF227:
	.ascii	"gVendorBlkInfo\000"
.LASF423:
	.ascii	"p_superblock\000"
.LASF271:
	.ascii	"p_data_block_list_head\000"
.LASF476:
	.ascii	"last_page_version\000"
.LASF232:
	.ascii	"req_gc\000"
.LASF611:
	.ascii	"FtlDiscard\000"
.LASF37:
	.ascii	"panic_notifier_list\000"
.LASF288:
	.ascii	"g_gc_next_blk\000"
.LASF387:
	.ascii	"add_count\000"
.LASF155:
	.ascii	"inkDie_write_and_check_en\000"
.LASF582:
	.ascii	"FtlGetLastWrittenPage\000"
.LASF466:
	.ascii	"detected_active_page\000"
.LASF284:
	.ascii	"g_gc_blk_num\000"
.LASF25:
	.ascii	"reset_devices\000"
.LASF158:
	.ascii	"refresh_enable_mode\000"
.LASF607:
	.ascii	"FtlBbmIsBadBlock\000"
.LASF177:
	.ascii	"dump_writed\000"
.LASF392:
	.ascii	"FtlGcRefreshBlock\000"
.LASF89:
	.ascii	"fBbtBlk\000"
.LASF537:
	.ascii	"blkTbl\000"
.LASF381:
	.ascii	"p_dataHeader\000"
.LASF463:
	.ascii	"saved_active_plane\000"
.LASF127:
	.ascii	"bufferFlashMode\000"
.LASF115:
	.ascii	"count\000"
.LASF134:
	.ascii	"gcTempPageOffset\000"
.LASF359:
	.ascii	"addr\000"
.LASF140:
	.ascii	"GlobalSysVersion\000"
.LASF412:
	.ascii	"src_ppa\000"
.LASF558:
	.ascii	"List_pop_index_node\000"
.LASF274:
	.ascii	"g_num_data_superblocks\000"
.LASF448:
	.ascii	"error_flag\000"
.LASF666:
	.ascii	"drivers/rkflash/rksftl/rk_sftl.c\000"
.LASF312:
	.ascii	"g_in_swl_replace\000"
.LASF170:
	.ascii	"ftl_superblock_info\000"
.LASF3:
	.ascii	"unsigned int\000"
.LASF99:
	.ascii	"pBlkTbl\000"
.LASF169:
	.ascii	"rear\000"
.LASF193:
	.ascii	"c_ftl_nand_bbm_buf_size\000"
.LASF220:
	.ascii	"g_MaxLpn\000"
.LASF542:
	.ascii	"blk_index\000"
.LASF457:
	.ascii	"num_data_node\000"
.LASF51:
	.ascii	"SYSTEM_HALT\000"
.LASF292:
	.ascii	"g_gc_bad_block_temp_tbl\000"
.LASF34:
	.ascii	"kmsg_fops\000"
.LASF569:
	.ascii	"INSERT_DATA_LIST\000"
.LASF616:
	.ascii	"sctidx\000"
.LASF391:
	.ascii	"superBlk\000"
.LASF184:
	.ascii	"flag\000"
.LASF285:
	.ascii	"g_gc_page_offset\000"
.LASF642:
	.ascii	"test_mode\000"
.LASF300:
	.ascii	"g_totle_discard_page_count\000"
.LASF218:
	.ascii	"g_GlobalDataVersion\000"
.LASF653:
	.ascii	"debug_flag\000"
.LASF328:
	.ascii	"gFtlInitStatus\000"
.LASF531:
	.ascii	"hit_count\000"
.LASF83:
	.ascii	"prog_page\000"
.LASF131:
	.ascii	"bufferSuperblockId\000"
.LASF493:
	.ascii	"FtlLoadSysInfo\000"
.LASF156:
	.ascii	"readErrorCount\000"
.LASF173:
	.ascii	"current_plane\000"
.LASF400:
	.ascii	"page_count\000"
.LASF278:
	.ascii	"g_gc_superblock\000"
.LASF275:
	.ascii	"g_active_superblock\000"
.LASF518:
	.ascii	"offset\000"
.LASF253:
	.ascii	"p_valid_page_count_table\000"
.LASF198:
	.ascii	"c_ftl_nand_sec_pre_page_shift\000"
.LASF27:
	.ascii	"initcall_debug\000"
.LASF393:
	.ascii	"block\000"
.LASF355:
	.ascii	"FlashEraseBlocks\000"
.LASF86:
	.ascii	"page\000"
.LASF414:
	.ascii	"IsBlkInGcList\000"
.LASF342:
	.ascii	"str1\000"
.LASF68:
	.ascii	"nand_type\000"
.LASF617:
	.ascii	"nscts\000"
.LASF105:
	.ascii	"ftl_l2p_ram_map_info\000"
.LASF298:
	.ascii	"g_totle_write_sector\000"
.LASF447:
	.ascii	"prev_ppa\000"
.LASF81:
	.ascii	"get_bad_blk_list\000"
.LASF297:
	.ascii	"g_totle_write_page_count\000"
.LASF647:
	.ascii	"FtlMemInit\000"
.LASF304:
	.ascii	"g_totle_l2p_write_count\000"
.LASF590:
	.ascii	"P2V_plane\000"
.LASF12:
	.ascii	"bool\000"
.LASF257:
	.ascii	"p_map_block_valid_page_count\000"
.LASF340:
	.ascii	"check_buf\000"
.LASF562:
	.ascii	"node_erase_count\000"
.LASF556:
	.ascii	"List_pop_head_node\000"
.LASF146:
	.ascii	"slc_erase_count\000"
.LASF545:
	.ascii	"minValidPageCount\000"
.LASF645:
	.ascii	"ven_blk\000"
.LASF194:
	.ascii	"c_ftl_nand_page_pre_blk\000"
.LASF108:
	.ascii	"L2PMap\000"
.LASF348:
	.ascii	"size\000"
.LASF548:
	.ascii	"List_update_data_list\000"
.LASF309:
	.ascii	"g_totle_sys_slc_erase_count\000"
.LASF125:
	.ascii	"maxDieNum\000"
.LASF247:
	.ascii	"g_gc_num_req\000"
.LASF92:
	.ascii	"maxLogicBlk\000"
.LASF151:
	.ascii	"totle_power_on_run_times\000"
.LASF522:
	.ascii	"log2phys\000"
.LASF470:
	.ascii	"detected_error_page\000"
.LASF584:
	.ascii	"spareBuf\000"
.LASF189:
	.ascii	"c_ftl_nand_blks_per_die_shift\000"
.LASF141:
	.ascii	"ftl_sys_ext_info\000"
.LASF266:
	.ascii	"p_l2p_ram_map\000"
.LASF98:
	.ascii	"maxBlkNum\000"
.LASF663:
	.ascii	"ftl_malloc\000"
.LASF445:
	.ascii	"pDataHeader\000"
.LASF171:
	.ascii	"current_page\000"
.LASF82:
	.ascii	"erase_blk\000"
.LASF456:
	.ascii	"SupperBlkListInit\000"
.LASF635:
	.ascii	"create_first_active_superblock\000"
.LASF167:
	.ascii	"sys_blk_queue\000"
.LASF35:
	.ascii	"file_operations\000"
.LASF361:
	.ascii	"FlashProgPages\000"
.LASF520:
	.ascii	"ppn_index\000"
.LASF9:
	.ascii	"long long unsigned int\000"
.LASF221:
	.ascii	"g_VaildLpn\000"
.LASF250:
	.ascii	"p_erase_count_table\000"
.LASF529:
	.ascii	"select_l2p_ram_region\000"
.LASF439:
	.ascii	"GetFreeBlockMaxEraseCount\000"
.LASF553:
	.ascii	"prev_multiplier_value\000"
.LASF22:
	.ascii	"__security_initcall_end\000"
.LASF41:
	.ascii	"panic_on_oops\000"
.LASF268:
	.ascii	"g_l2p_last_update_region_id\000"
.LASF202:
	.ascii	"c_ftl_nand_totle_phy_blks\000"
.LASF637:
	.ascii	"FtlLowFormatEraseBlock\000"
.LASF77:
	.ascii	"byte_per_sec\000"
.LASF182:
	.ascii	"des_ppa\000"
.LASF256:
	.ascii	"p_map_block_table\000"
.LASF17:
	.ascii	"soc_mb\000"
.LASF549:
	.ascii	"prev_node_id\000"
.LASF244:
	.ascii	"p_gc_data_buf\000"
.LASF533:
	.ascii	"FtlMapWritePage\000"
.LASF16:
	.ascii	"elf_hwcap2\000"
.LASF255:
	.ascii	"p_blk_mode_table\000"
.LASF286:
	.ascii	"g_gc_cur_blk_valid_pages\000"
.LASF580:
	.ascii	"FtlFreeSysBlkQueueEmpty\000"
.LASF195:
	.ascii	"c_ftl_nand_page_pre_slc_blk\000"
.LASF516:
	.ascii	"nSec\000"
.LASF598:
	.ascii	"FtlBbtCalcTotleCnt\000"
.LASF197:
	.ascii	"c_ftl_nand_sec_pre_page\000"
.LASF496:
	.ascii	"FtlSlcSuperblockCheck\000"
.LASF587:
	.ascii	"ver1\000"
.LASF154:
	.ascii	"SlcPartLbaEndSector\000"
.LASF207:
	.ascii	"c_ftl_nand_max_map_blks\000"
.LASF498:
	.ascii	"FtlGcReFreshBadBlk\000"
.LASF477:
	.ascii	"last_mlc_page_version\000"
.LASF509:
	.ascii	"FtlMapBlkWriteDump_data\000"
.LASF249:
	.ascii	"g_ect_tbl_info_size\000"
.LASF111:
	.ascii	"totleEc\000"
.LASF441:
	.ascii	"GetFreeBlockMinEraseCount\000"
.LASF438:
	.ascii	"minDataBlockEraseCount\000"
.LASF52:
	.ascii	"SYSTEM_POWER_OFF\000"
.LASF219:
	.ascii	"g_MaxLbaSector\000"
.LASF88:
	.ascii	"version\000"
.LASF32:
	.ascii	"dmesg_restrict\000"
.LASF465:
	.ascii	"next_free_active_page\000"
.LASF133:
	.ascii	"gcTempSuperblockId\000"
.LASF624:
	.ascii	"first_lpa_nscts\000"
.LASF303:
	.ascii	"g_totle_cache_write_count\000"
.LASF497:
	.ascii	"ftl_set_blk_mode\000"
.LASF453:
	.ascii	"Ftl_load_ext_data\000"
.LASF507:
	.ascii	"FtlWriteDump_data\000"
.LASF639:
	.ascii	"max_test_page_num\000"
.LASF394:
	.ascii	"Ftl_gc_temp_data_write_back\000"
.LASF370:
	.ascii	"block_in_die\000"
.LASF419:
	.ascii	"FtlGcBufInit\000"
.LASF235:
	.ascii	"p_plane_order_table\000"
.LASF102:
	.ascii	"pMapPpnTbl\000"
.LASF431:
	.ascii	"free_data_superblock\000"
.LASF147:
	.ascii	"sys_slc_erase_count\000"
.LASF208:
	.ascii	"c_ftl_nand_max_vendor_blks\000"
.LASF379:
	.ascii	"physical_block\000"
.LASF449:
	.ascii	"FtlVpcCheckAndModify\000"
.LASF280:
	.ascii	"g_sys_save_data\000"
.LASF85:
	.ascii	"ftl_bbt_info\000"
.LASF429:
	.ascii	"free_blk_index\000"
.LASF254:
	.ascii	"p_valid_page_count_check_table\000"
.LASF213:
	.ascii	"c_ftl_nand_data_blks_per_plane\000"
.LASF372:
	.ascii	"rk_ftl_garbage_collect\000"
.LASF658:
	.ascii	"memcmp\000"
.LASF374:
	.ascii	"numPages\000"
.LASF315:
	.ascii	"g_gc_head_data_block_count\000"
.LASF259:
	.ascii	"p_map_region_ppn_table\000"
.LASF528:
	.ascii	"l2p_flush\000"
.LASF459:
	.ascii	"make_superblock\000"
.LASF20:
	.ascii	"__con_initcall_end\000"
.LASF633:
	.ascii	"bad_block_cnt\000"
.LASF551:
	.ascii	"node_multiplier_value\000"
.LASF334:
	.ascii	"g_ect_tbl_power_up_flush\000"
.LASF320:
	.ascii	"g_recovery_ppa_tbl\000"
.LASF527:
	.ascii	"found_lpa\000"
.LASF281:
	.ascii	"g_sys_ext_data\000"
.LASF357:
	.ascii	"flash_type\000"
.LASF417:
	.ascii	"req_num\000"
.LASF512:
	.ascii	"forceFlush\000"
.LASF57:
	.ascii	"uint8\000"
.LASF333:
	.ascii	"power_up_flag\000"
.LASF283:
	.ascii	"p_gc_blk_tbl\000"
.LASF415:
	.ascii	"FtlGcBufAlloc\000"
.LASF192:
	.ascii	"c_ftl_nand_ext_blk_pre_plane\000"
.LASF380:
	.ascii	"max_gc_page_num\000"
.LASF199:
	.ascii	"c_ftl_nand_byte_pre_page\000"
.LASF442:
	.ascii	"update_multiplier_value\000"
.LASF620:
	.ascii	"flashType\000"
.LASF6:
	.ascii	"short int\000"
.LASF243:
	.ascii	"p_gc_spare_buf\000"
.LASF495:
	.ascii	"sblk\000"
.LASF373:
	.ascii	"mode\000"
.LASF427:
	.ascii	"new_id\000"
.LASF630:
	.ascii	"pNand\000"
.LASF50:
	.ascii	"SYSTEM_RUNNING\000"
.LASF517:
	.ascii	"pBuf\000"
.LASF143:
	.ascii	"write_page_count\000"
.LASF605:
	.ascii	"bitmap\000"
.LASF201:
	.ascii	"c_ftl_nand_reserved_blks\000"
.LASF670:
	.ascii	"sftl_get_density\000"
.LASF641:
	.ascii	"test_page_step\000"
.LASF596:
	.ascii	"blkAddr\000"
.LASF287:
	.ascii	"g_gc_cur_blk_max_valid_pages\000"
.LASF289:
	.ascii	"g_gc_next_blk_1\000"
.LASF530:
	.ascii	"target_region\000"
.LASF614:
	.ascii	"end_lpa\000"
.LASF474:
	.ascii	"scan_completed\000"
.LASF657:
	.ascii	"FtlPrintInfo2buf\000"
.LASF366:
	.ascii	"p_die\000"
.LASF240:
	.ascii	"p_io_data_buf_0\000"
.LASF241:
	.ascii	"p_io_data_buf_1\000"
.LASF138:
	.ascii	"mlc_erase_count\000"
.LASF179:
	.ascii	"phyBlk\000"
.LASF627:
	.ascii	"sftl_deinit\000"
.LASF487:
	.ascii	"pMapBlockInfo\000"
.LASF152:
	.ascii	"last_refresh_data_times\000"
.LASF668:
	.ascii	"system_states\000"
.LASF160:
	.ascii	"totle_write_sector\000"
.LASF230:
	.ascii	"req_prgm\000"
.LASF592:
	.ascii	"FtlMakeBbt\000"
.LASF103:
	.ascii	"in_gc_mode\000"
.LASF435:
	.ascii	"min_ec\000"
.LASF61:
	.ascii	"int16\000"
.LASF282:
	.ascii	"p_gc_page_info\000"
.LASF212:
	.ascii	"c_ftl_nand_max_sys_blks\000"
.LASF440:
	.ascii	"max_ec\000"
.LASF239:
	.ascii	"p_sys_spare_buf\000"
.LASF671:
	.ascii	"memcpy\000"
.LASF505:
	.ascii	"prog_error_count\000"
.LASF337:
	.ascii	"gc_discard_updated\000"
.LASF413:
	.ascii	"prev_superblock_id\000"
.LASF640:
	.ascii	"test_page_num\000"
.LASF157:
	.ascii	"last_refresh_read_count\000"
.LASF479:
	.ascii	"lookup_ppa_ver\000"
.LASF491:
	.ascii	"totleBlkNum\000"
.LASF610:
	.ascii	"sftl_read\000"
.LASF579:
	.ascii	"FtlFreeSysBlkQueueFull\000"
.LASF410:
	.ascii	"FtlGcPageVarInit\000"
.LASF113:
	.ascii	"padding\000"
.LASF175:
	.ascii	"flash_mode\000"
.LASF469:
	.ascii	"recovery_cur_page_ver\000"
.LASF538:
	.ascii	"ftl_map_blk_gc\000"
.LASF609:
	.ascii	"sftl_write\000"
.LASF119:
	.ascii	"ftl_map_blk_header\000"
.LASF581:
	.ascii	"FtlFreeSysBlkQueueInit\000"
.LASF26:
	.ascii	"late_time_init\000"
.LASF648:
	.ascii	"mem_size\000"
.LASF486:
	.ascii	"FtlMapTblRecovery\000"
.LASF424:
	.ascii	"new_ppa\000"
.LASF428:
	.ascii	"num_bad_block\000"
.LASF526:
	.ascii	"lpn_index\000"
.LASF204:
	.ascii	"c_ftl_nand_l2pmap_ram_region_num\000"
.LASF626:
	.ascii	"sector\000"
.LASF508:
	.ascii	"num_page\000"
.LASF619:
	.ascii	"FtlProgPages\000"
.LASF418:
	.ascii	"FtlGcBufFree\000"
.LASF272:
	.ascii	"p_data_block_list_tail\000"
.LASF399:
	.ascii	"current_ppa\000"
.LASF65:
	.ascii	"p_spare\000"
.LASF384:
	.ascii	"tmp_blk\000"
.LASF295:
	.ascii	"g_gc_blk_index\000"
.LASF501:
	.ascii	"block_in_plane\000"
.LASF112:
	.ascii	"lastEc\000"
.LASF353:
	.ascii	"PhyBlk\000"
.LASF107:
	.ascii	"reserved\000"
.LASF318:
	.ascii	"g_recovery_page_num\000"
.LASF550:
	.ascii	"node_valid_page_count\000"
.LASF180:
	.ascii	"ftl_gc_page_item\000"
.LASF144:
	.ascii	"read_page_count\000"
.LASF420:
	.ascii	"decrement_vpc_count\000"
.LASF7:
	.ascii	"short unsigned int\000"
.LASF540:
	.ascii	"minValidPageIndex\000"
.LASF332:
	.ascii	"TotleReadBufferMatchCount\000"
.LASF341:
	.ascii	"check_spare_buf\000"
.LASF575:
	.ascii	"tmp_ec\000"
.LASF316:
	.ascii	"g_gc_skip_write_count\000"
.LASF60:
	.ascii	"int32\000"
.LASF74:
	.ascii	"page_per_slc_blk\000"
.LASF319:
	.ascii	"g_recovery_page_min_ver\000"
.LASF228:
	.ascii	"req_sys\000"
.LASF163:
	.ascii	"List_Node\000"
.LASF654:
	.ascii	"Ftl_log2\000"
.LASF153:
	.ascii	"all_blk_used_slc_mode\000"
.LASF397:
	.ascii	"pSuperblock\000"
.LASF475:
	.ascii	"function_exit\000"
.LASF206:
	.ascii	"c_ftl_nand_map_blks_per_plane\000"
.LASF18:
	.ascii	"initcall_t\000"
.LASF591:
	.ascii	"V2P_block\000"
.LASF116:
	.ascii	"sysBlksPerPlane\000"
.LASF455:
	.ascii	"Ftl_save_ext_data\000"
.LASF618:
	.ascii	"FtlCacheWriteBack\000"
.LASF80:
	.ascii	"nand_ops\000"
.LASF631:
	.ascii	"load_bbt\000"
.LASF651:
	.ascii	"blkNum\000"
.LASF368:
	.ascii	"page_index\000"
.LASF503:
	.ascii	"FtlVpcTblFlush\000"
.LASF110:
	.ascii	"hash\000"
.LASF572:
	.ascii	"IsInFreeQueue\000"
.LASF472:
	.ascii	"read_super_page_end\000"
.LASF358:
	.ascii	"num_req\000"
.LASF321:
	.ascii	"c_mlc_erase_count_value\000"
.LASF554:
	.ascii	"pPrev\000"
.LASF139:
	.ascii	"GlobalDataVersion\000"
.LASF42:
	.ascii	"panic_on_unrecovered_nmi\000"
.LASF164:
	.ascii	"next\000"
.LASF129:
	.ascii	"activeSuperblockId\000"
.LASF377:
	.ascii	"plane\000"
.LASF44:
	.ascii	"panic_on_warn\000"
.LASF93:
	.ascii	"ftl_l2p_map_info\000"
.LASF165:
	.ascii	"prev\000"
.LASF58:
	.ascii	"uint16\000"
.LASF351:
	.ascii	"ftl_memset\000"
.LASF535:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
.LASF46:
	.ascii	"crash_kexec_post_notifiers\000"
.LASF290:
	.ascii	"g_gc_bad_block_temp_num\000"
.LASF124:
	.ascii	"ftl_sys_save_info\000"
.LASF279:
	.ascii	"gp_last_act_superblock\000"
.LASF577:
	.ascii	"erase_flag\000"
.LASF488:
	.ascii	"blk_id\000"
.LASF338:
	.ascii	"ftl_gc_temp_block_bops_scan_page_addr\000"
