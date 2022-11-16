/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 */

	.arch armv8-a
	.file	"fixup_fault.c"
	.text
	.align	2
	.p2align 3,,7
	.type	get_data, %function
get_data:
	cmp	w0, 31
	hint	25 // paciasp
	bls	.L39
	mov	x0, 0
.L1:
	hint	29 // autiasp
	ret
	.p2align 2,,3
.L39:
	adrp	x2, .L4
	add	x2, x2, :lo12:.L4
	ldrh	w0, [x2,w0,uxtw #1]
	adr	x2, .Lrtx4
	add	x0, x2, w0, sxth #2
	br	x0
.Lrtx4:
	.section	.rodata
	.align	0
	.align	2
.L4:
	.2byte	(.L35 - .Lrtx4) / 4
	.2byte	(.L34 - .Lrtx4) / 4
	.2byte	(.L33 - .Lrtx4) / 4
	.2byte	(.L32 - .Lrtx4) / 4
	.2byte	(.L31 - .Lrtx4) / 4
	.2byte	(.L30 - .Lrtx4) / 4
	.2byte	(.L29 - .Lrtx4) / 4
	.2byte	(.L28 - .Lrtx4) / 4
	.2byte	(.L27 - .Lrtx4) / 4
	.2byte	(.L26 - .Lrtx4) / 4
	.2byte	(.L25 - .Lrtx4) / 4
	.2byte	(.L24 - .Lrtx4) / 4
	.2byte	(.L23 - .Lrtx4) / 4
	.2byte	(.L22 - .Lrtx4) / 4
	.2byte	(.L21 - .Lrtx4) / 4
	.2byte	(.L20 - .Lrtx4) / 4
	.2byte	(.L19 - .Lrtx4) / 4
	.2byte	(.L18 - .Lrtx4) / 4
	.2byte	(.L17 - .Lrtx4) / 4
	.2byte	(.L16 - .Lrtx4) / 4
	.2byte	(.L15 - .Lrtx4) / 4
	.2byte	(.L14 - .Lrtx4) / 4
	.2byte	(.L13 - .Lrtx4) / 4
	.2byte	(.L12 - .Lrtx4) / 4
	.2byte	(.L11 - .Lrtx4) / 4
	.2byte	(.L10 - .Lrtx4) / 4
	.2byte	(.L9 - .Lrtx4) / 4
	.2byte	(.L8 - .Lrtx4) / 4
	.2byte	(.L7 - .Lrtx4) / 4
	.2byte	(.L6 - .Lrtx4) / 4
	.2byte	(.L5 - .Lrtx4) / 4
	.2byte	(.L3 - .Lrtx4) / 4
	.text
	.p2align 2,,3
.L5:
#APP
	cbnz w1, 1f
	mov x0, v30.d[0]
	b 2f
	1: mov x0, v30.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L6:
#APP
	cbnz w1, 1f
	mov x0, v29.d[0]
	b 2f
	1: mov x0, v29.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L7:
#APP
	cbnz w1, 1f
	mov x0, v28.d[0]
	b 2f
	1: mov x0, v28.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L8:
#APP
	cbnz w1, 1f
	mov x0, v27.d[0]
	b 2f
	1: mov x0, v27.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L9:
#APP
	cbnz w1, 1f
	mov x0, v26.d[0]
	b 2f
	1: mov x0, v26.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L10:
#APP
	cbnz w1, 1f
	mov x0, v25.d[0]
	b 2f
	1: mov x0, v25.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L11:
#APP
	cbnz w1, 1f
	mov x0, v24.d[0]
	b 2f
	1: mov x0, v24.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L12:
#APP
	cbnz w1, 1f
	mov x0, v23.d[0]
	b 2f
	1: mov x0, v23.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L13:
#APP
	cbnz w1, 1f
	mov x0, v22.d[0]
	b 2f
	1: mov x0, v22.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L14:
#APP
	cbnz w1, 1f
	mov x0, v21.d[0]
	b 2f
	1: mov x0, v21.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L15:
#APP
	cbnz w1, 1f
	mov x0, v20.d[0]
	b 2f
	1: mov x0, v20.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L16:
#APP
	cbnz w1, 1f
	mov x0, v19.d[0]
	b 2f
	1: mov x0, v19.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L17:
#APP
	cbnz w1, 1f
	mov x0, v18.d[0]
	b 2f
	1: mov x0, v18.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L18:
#APP
	cbnz w1, 1f
	mov x0, v17.d[0]
	b 2f
	1: mov x0, v17.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L19:
#APP
	cbnz w1, 1f
	mov x0, v16.d[0]
	b 2f
	1: mov x0, v16.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L20:
#APP
	cbnz w1, 1f
	mov x0, v15.d[0]
	b 2f
	1: mov x0, v15.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L21:
#APP
	cbnz w1, 1f
	mov x0, v14.d[0]
	b 2f
	1: mov x0, v14.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L22:
#APP
	cbnz w1, 1f
	mov x0, v13.d[0]
	b 2f
	1: mov x0, v13.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L23:
#APP
	cbnz w1, 1f
	mov x0, v12.d[0]
	b 2f
	1: mov x0, v12.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L24:
#APP
	cbnz w1, 1f
	mov x0, v11.d[0]
	b 2f
	1: mov x0, v11.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L25:
#APP
	cbnz w1, 1f
	mov x0, v10.d[0]
	b 2f
	1: mov x0, v10.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L26:
#APP
	cbnz w1, 1f
	mov x0, v9.d[0]
	b 2f
	1: mov x0, v9.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L27:
#APP
	cbnz w1, 1f
	mov x0, v8.d[0]
	b 2f
	1: mov x0, v8.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L28:
#APP
	cbnz w1, 1f
	mov x0, v7.d[0]
	b 2f
	1: mov x0, v7.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L29:
#APP
	cbnz w1, 1f
	mov x0, v6.d[0]
	b 2f
	1: mov x0, v6.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L30:
#APP
	cbnz w1, 1f
	mov x0, v5.d[0]
	b 2f
	1: mov x0, v5.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L31:
#APP
	cbnz w1, 1f
	mov x0, v4.d[0]
	b 2f
	1: mov x0, v4.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L32:
#APP
	cbnz w1, 1f
	mov x0, v3.d[0]
	b 2f
	1: mov x0, v3.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L33:
#APP
	cbnz w1, 1f
	mov x0, v2.d[0]
	b 2f
	1: mov x0, v2.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L34:
#APP
	cbnz w1, 1f
	mov x0, v1.d[0]
	b 2f
	1: mov x0, v1.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L35:
#APP
	cbnz w1, 1f
	mov x0, v0.d[0]
	b 2f
	1: mov x0, v0.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.p2align 2,,3
.L3:
#APP
	cbnz w1, 1f
	mov x0, v31.d[0]
	b 2f
	1: mov x0, v31.d[1]
	2:
// 0 "" 2
#NO_APP
	b	.L1
	.size	get_data, .-get_data
	.align	2
	.p2align 3,,7
	.type	set_data, %function
set_data:
	cmp	w0, 31
	hint	25 // paciasp
	bhi	.L40
	adrp	x3, .L43
	add	x3, x3, :lo12:.L43
	ldrh	w0, [x3,w0,uxtw #1]
	adr	x3, .Lrtx43
	add	x0, x3, w0, sxth #2
	br	x0
.Lrtx43:
	.section	.rodata
	.align	0
	.align	2
.L43:
	.2byte	(.L74 - .Lrtx43) / 4
	.2byte	(.L73 - .Lrtx43) / 4
	.2byte	(.L72 - .Lrtx43) / 4
	.2byte	(.L71 - .Lrtx43) / 4
	.2byte	(.L70 - .Lrtx43) / 4
	.2byte	(.L69 - .Lrtx43) / 4
	.2byte	(.L68 - .Lrtx43) / 4
	.2byte	(.L67 - .Lrtx43) / 4
	.2byte	(.L66 - .Lrtx43) / 4
	.2byte	(.L65 - .Lrtx43) / 4
	.2byte	(.L64 - .Lrtx43) / 4
	.2byte	(.L63 - .Lrtx43) / 4
	.2byte	(.L62 - .Lrtx43) / 4
	.2byte	(.L61 - .Lrtx43) / 4
	.2byte	(.L60 - .Lrtx43) / 4
	.2byte	(.L59 - .Lrtx43) / 4
	.2byte	(.L58 - .Lrtx43) / 4
	.2byte	(.L57 - .Lrtx43) / 4
	.2byte	(.L56 - .Lrtx43) / 4
	.2byte	(.L55 - .Lrtx43) / 4
	.2byte	(.L54 - .Lrtx43) / 4
	.2byte	(.L53 - .Lrtx43) / 4
	.2byte	(.L52 - .Lrtx43) / 4
	.2byte	(.L51 - .Lrtx43) / 4
	.2byte	(.L50 - .Lrtx43) / 4
	.2byte	(.L49 - .Lrtx43) / 4
	.2byte	(.L48 - .Lrtx43) / 4
	.2byte	(.L47 - .Lrtx43) / 4
	.2byte	(.L46 - .Lrtx43) / 4
	.2byte	(.L45 - .Lrtx43) / 4
	.2byte	(.L44 - .Lrtx43) / 4
	.2byte	(.L42 - .Lrtx43) / 4
	.text
	.p2align 2,,3
.L42:
#APP
	cbnz w1, 1f
	mov v31.d[0], x2
	b 2f
	1: mov v31.d[1], x2
	2:
// 0 "" 2
#NO_APP
.L40:
	hint	29 // autiasp
	ret
	.p2align 2,,3
.L44:
#APP
	cbnz w1, 1f
	mov v30.d[0], x2
	b 2f
	1: mov v30.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L45:
#APP
	cbnz w1, 1f
	mov v29.d[0], x2
	b 2f
	1: mov v29.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L46:
#APP
	cbnz w1, 1f
	mov v28.d[0], x2
	b 2f
	1: mov v28.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L47:
#APP
	cbnz w1, 1f
	mov v27.d[0], x2
	b 2f
	1: mov v27.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L48:
#APP
	cbnz w1, 1f
	mov v26.d[0], x2
	b 2f
	1: mov v26.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L49:
#APP
	cbnz w1, 1f
	mov v25.d[0], x2
	b 2f
	1: mov v25.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L50:
#APP
	cbnz w1, 1f
	mov v24.d[0], x2
	b 2f
	1: mov v24.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L51:
#APP
	cbnz w1, 1f
	mov v23.d[0], x2
	b 2f
	1: mov v23.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L52:
#APP
	cbnz w1, 1f
	mov v22.d[0], x2
	b 2f
	1: mov v22.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L53:
#APP
	cbnz w1, 1f
	mov v21.d[0], x2
	b 2f
	1: mov v21.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L54:
#APP
	cbnz w1, 1f
	mov v20.d[0], x2
	b 2f
	1: mov v20.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L55:
#APP
	cbnz w1, 1f
	mov v19.d[0], x2
	b 2f
	1: mov v19.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L56:
#APP
	cbnz w1, 1f
	mov v18.d[0], x2
	b 2f
	1: mov v18.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L57:
#APP
	cbnz w1, 1f
	mov v17.d[0], x2
	b 2f
	1: mov v17.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L58:
#APP
	cbnz w1, 1f
	mov v16.d[0], x2
	b 2f
	1: mov v16.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L59:
#APP
	cbnz w1, 1f
	mov v15.d[0], x2
	b 2f
	1: mov v15.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L60:
#APP
	cbnz w1, 1f
	mov v14.d[0], x2
	b 2f
	1: mov v14.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L61:
#APP
	cbnz w1, 1f
	mov v13.d[0], x2
	b 2f
	1: mov v13.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L62:
#APP
	cbnz w1, 1f
	mov v12.d[0], x2
	b 2f
	1: mov v12.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L63:
#APP
	cbnz w1, 1f
	mov v11.d[0], x2
	b 2f
	1: mov v11.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L64:
#APP
	cbnz w1, 1f
	mov v10.d[0], x2
	b 2f
	1: mov v10.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L65:
#APP
	cbnz w1, 1f
	mov v9.d[0], x2
	b 2f
	1: mov v9.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L66:
#APP
	cbnz w1, 1f
	mov v8.d[0], x2
	b 2f
	1: mov v8.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L67:
#APP
	cbnz w1, 1f
	mov v7.d[0], x2
	b 2f
	1: mov v7.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L68:
#APP
	cbnz w1, 1f
	mov v6.d[0], x2
	b 2f
	1: mov v6.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L69:
#APP
	cbnz w1, 1f
	mov v5.d[0], x2
	b 2f
	1: mov v5.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L70:
#APP
	cbnz w1, 1f
	mov v4.d[0], x2
	b 2f
	1: mov v4.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L71:
#APP
	cbnz w1, 1f
	mov v3.d[0], x2
	b 2f
	1: mov v3.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L72:
#APP
	cbnz w1, 1f
	mov v2.d[0], x2
	b 2f
	1: mov v2.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L73:
#APP
	cbnz w1, 1f
	mov v1.d[0], x2
	b 2f
	1: mov v1.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.p2align 2,,3
.L74:
#APP
	cbnz w1, 1f
	mov v0.d[0], x2
	b 2f
	1: mov v0.d[1], x2
	2:
// 0 "" 2
#NO_APP
	b	.L40
	.size	set_data, .-set_data
	.align	2
	.p2align 3,,7
	.type	rk_align_wr, %function
rk_align_wr:
	hint	25 // paciasp
	stp	x29, x30, [sp, -48]!
	cmp	w1, 4
	mov	x29, sp
	str	x19, [sp, 16]
	mrs	x19, sp_el0
	ldr	x4, [x19, 1368]
	str	x4, [sp, 40]
	mov	x4,0
	beq	.L77
	bgt	.L78
	cmp	w1, 1
	beq	.L79
	cmp	w1, 2
	bne	.L88
	strh	w2, [sp, 32]
	mov	x3, 4294963200
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x2, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x2]
	tst	w2, 4194304
	bne	.L84
.L126:
	adrp	x2, vabits_actual
	mov	x3, 1
	ldr	x2, [x2, #:lo12:vabits_actual]
	lsl	x3, x3, x2
.L84:
	sxtw	x2, w1
	cmp	x0, x3
	bcs	.L85
	add	x8, sp, 32
	mov	x4, x0
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x5, sp_el0
// 0 "" 2
#NO_APP
	lsl	x3, x0, 8
	sub	x8, x8, x0
	ldr	w0, [x5, 52]
	mov	x13, x5
	mov	x12, x5
	adrp	x7, cpu_hwcaps
	mov	w11, 0
	ldr	x1, [x5, 8]
	tbnz	x0, 21, .L86
	.p2align 3,,7
.L123:
	ldr	x6, [x5]
	mov	x0, x4
	tbnz	x6, 26, .L86
#APP
// 79 "./arch/arm64/include/asm/uaccess.h" 1
		adds	x0, x0, 1
	csel	x1, xzr, x1, hi
	csinv	x0, x0, xzr, cc
	sbcs	xzr, x0, x1
	cset	x0, ls

// 0 "" 2
#NO_APP
	cbz	x0, .L88
.L124:
	and	x0, x4, x3, asr 8
	ldr	x1, [x13, 8]
#APP
// 289 "./arch/arm64/include/asm/uaccess.h" 1
		bics	xzr, x0, x1
	csel	x9, x4, xzr, eq

// 0 "" 2
// 297 "./arch/arm64/include/asm/uaccess.h" 1
	hint #20
// 0 "" 2
#NO_APP
	ldrb	w6, [x8, x4]
#APP
// 38 "./arch/arm64/include/asm/jump_label.h" 1
	1:	b		.L89		
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L89 - .	
		.quad		arm64_const_caps_ready+1 - .			
		.popsection				
	
// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L91 - .	
		.quad		cpu_hwcap_keys+64 - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L92:
#APP
// 72 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	mrs	x0, daif
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x0, (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5))
	.purgem	mrs_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 87 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	and	w1, w0, #0x00000080
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	eor	w1, w0, #0xe0
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	cbz	w1, .L121
.L94:
	ldr	x1, [x12, 16]
#APP
// 141 "./arch/arm64/include/asm/uaccess.h" 1
	mrs x10, ttbr1_el1
// 0 "" 2
#NO_APP
	mov	x14, x1
	bfi	x14, x10, 0, 48
#APP
// 144 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr1_el1, x14
// 0 "" 2
// 145 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 148 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr0_el1, x1
// 0 "" 2
// 149 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 124 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daif, x0
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x0
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L96 - .	
		.quad		gic_pmr_sync - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L93:
	mov	w0, w11
#APP
	1:	sttrb	w6, [x9]
2:
	.section .fixup,"ax"
	.align	2
3:	mov	w0, -14
	b	2b
	.previous
	.pushsection	__ex_table, "a"
	.align		3
	.long		(1b - .), (3b - .)
	.popsection

// 0 "" 2
// 38 "./arch/arm64/include/asm/jump_label.h" 1
	1:	b		.L97		
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L97 - .	
		.quad		arm64_const_caps_ready+1 - .			
		.popsection				
	
// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L98 - .	
		.quad		cpu_hwcap_keys+64 - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L100:
#APP
// 72 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	mrs	x6, daif
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x6, (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5))
	.purgem	mrs_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 87 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	and	w1, w6, #0x00000080
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	eor	w1, w6, #0xe0
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	cbz	w1, .L122
.L102:
#APP
// 117 "./arch/arm64/include/asm/uaccess.h" 1
	mrs x1, ttbr1_el1
// 0 "" 2
#NO_APP
	and	x1, x1, 281474976710655
	sub	x9, x1, #4096
#APP
// 120 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr0_el1, x9
// 0 "" 2
// 121 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 123 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr1_el1, x1
// 0 "" 2
// 124 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 124 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daif, x6
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x6
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L104 - .	
		.quad		gic_pmr_sync - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L101:
	cbnz	w0, .L88
	add	x4, x4, 1
	add	x3, x3, 256
	subs	x2, x2, #1
	beq	.L76
	ldr	w0, [x5, 52]
	ldr	x1, [x5, 8]
	tbz	x0, 21, .L123
.L86:
	and	x0, x4, x3, asr 8
#APP
// 79 "./arch/arm64/include/asm/uaccess.h" 1
		adds	x0, x0, 1
	csel	x1, xzr, x1, hi
	csinv	x0, x0, xzr, cc
	sbcs	xzr, x0, x1
	cset	x0, ls

// 0 "" 2
#NO_APP
	cbnz	x0, .L124
	.p2align 3,,7
.L88:
	mov	w0, 1
.L76:
	add	x19, x19, 1368
	ldr	x1, [sp, 40]
	ldr	x2, [x19]
	subs	x1, x1, x2
	mov	x2, 0
	bne	.L125
	ldr	x19, [sp, 16]
	ldp	x29, x30, [sp], 48
	hint	29 // autiasp
	ret
	.p2align 2,,3
.L78:
	cmp	w1, 8
	bne	.L88
	str	x2, [sp, 32]
	mov	x3, 4294963200
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x2, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x2]
	tst	w2, 4194304
	bne	.L84
	b	.L126
	.p2align 2,,3
.L85:
	add	x1, sp, 32
	bl	__memcpy_toio
	mov	w0, 0
	b	.L76
	.p2align 2,,3
.L97:
	ldr	x1, [x7, #:lo12:cpu_hwcaps]
	tst	w1, 16
	beq	.L100
.L98:
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
	b	.L101
	.p2align 2,,3
.L89:
	ldr	x0, [x7, #:lo12:cpu_hwcaps]
	tst	w0, 16
	beq	.L92
.L91:
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
	b	.L93
	.p2align 2,,3
.L96:
#APP
// 132 "./arch/arm64/include/asm/irqflags.h" 1
	dsb sy
// 0 "" 2
#NO_APP
	b	.L93
	.p2align 2,,3
.L104:
#APP
// 132 "./arch/arm64/include/asm/irqflags.h" 1
	dsb sy
// 0 "" 2
#NO_APP
	b	.L101
	.p2align 2,,3
.L122:
#APP
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L110 - .	
		.quad		gic_nonsecure_priorities - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	mov	x1, 96
.L103:
#APP
// 56 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daifset, #2		// arch_local_irq_disable
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x1
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	b	.L102
	.p2align 2,,3
.L121:
#APP
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L109 - .	
		.quad		gic_nonsecure_priorities - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	mov	x1, 96
.L95:
#APP
// 56 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daifset, #2		// arch_local_irq_disable
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x1
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	b	.L94
	.p2align 2,,3
.L110:
	mov	x1, 160
	b	.L103
	.p2align 2,,3
.L109:
	mov	x1, 160
	b	.L95
	.p2align 2,,3
.L79:
	strb	w2, [sp, 32]
	mov	x3, 4294963200
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x2, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x2]
	tst	w2, 4194304
	bne	.L84
	b	.L126
	.p2align 2,,3
.L77:
	str	w2, [sp, 32]
	mov	x3, 4294963200
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x2, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x2]
	tst	w2, 4194304
	bne	.L84
	b	.L126
.L125:
	bl	__stack_chk_fail
	.size	rk_align_wr, .-rk_align_wr
	.align	2
	.p2align 3,,7
	.type	rk_align_rd, %function
rk_align_rd:
	hint	25 // paciasp
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mrs	x20, sp_el0
	mov	w19, w1
	ldr	x1, [x20, 1368]
	str	x1, [sp, 56]
	mov	x1,0
	sub	w3, w19, #1
	sub	w1, w19, #4
	tst	w1, -5
	ccmp	w3, 1, 0, ne
	bhi	.L158
	str	x21, [sp, 32]
	mov	x1, x0
	mov	x21, x2
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x0, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x0]
	mov	x0, 4294963200
	tst	w2, 4194304
	bne	.L130
	adrp	x2, vabits_actual
	mov	x0, 1
	ldr	x2, [x2, #:lo12:vabits_actual]
	lsl	x0, x0, x2
.L130:
	cmp	x1, x0
	sxtw	x2, w19
	bcc	.L176
	add	x0, sp, 48
	bl	__memcpy_fromio
.L152:
	cmp	w19, 4
	beq	.L153
	bgt	.L154
	cmp	w19, 1
	beq	.L155
	cmp	w19, 2
	bne	.L174
	ldrh	w1, [sp, 48]
	mov	w0, 0
	str	x1, [x21]
	ldr	x21, [sp, 32]
	.p2align 3,,7
.L127:
	add	x20, x20, 1368
	ldr	x1, [sp, 56]
	ldr	x2, [x20]
	subs	x1, x1, x2
	mov	x2, 0
	bne	.L177
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 64
	hint	29 // autiasp
	ret
	.p2align 2,,3
.L176:
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x5, sp_el0
// 0 "" 2
#NO_APP
	ldr	w0, [x5, 52]
	lsl	x4, x1, 8
	mov	x13, x5
	mov	x12, x5
	add	x9, sp, 48
	adrp	x8, cpu_hwcaps
	mov	w11, 0
	ldr	x3, [x5, 8]
	tbnz	x0, 21, .L132
	.p2align 3,,7
.L180:
	ldr	x6, [x5]
	mov	x0, x1
	tbnz	x6, 26, .L132
#APP
// 79 "./arch/arm64/include/asm/uaccess.h" 1
		adds	x0, x0, 1
	csel	x3, xzr, x3, hi
	csinv	x0, x0, xzr, cc
	sbcs	xzr, x0, x3
	cset	x0, ls

// 0 "" 2
#NO_APP
	cbz	x0, .L173
.L181:
	and	x0, x1, x4, asr 8
	ldr	x6, [x13, 8]
#APP
// 289 "./arch/arm64/include/asm/uaccess.h" 1
		bics	xzr, x0, x6
	csel	x3, x1, xzr, eq

// 0 "" 2
// 297 "./arch/arm64/include/asm/uaccess.h" 1
	hint #20
// 0 "" 2
// 38 "./arch/arm64/include/asm/jump_label.h" 1
	1:	b		.L135		
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L135 - .	
		.quad		arm64_const_caps_ready+1 - .			
		.popsection				
	
// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L137 - .	
		.quad		cpu_hwcap_keys+64 - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L138:
#APP
// 72 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	mrs	x0, daif
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x0, (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5))
	.purgem	mrs_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 87 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	and	w6, w0, #0x00000080
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	eor	w6, w0, #0xe0
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	cbz	w6, .L178
.L140:
	ldr	x6, [x12, 16]
#APP
// 141 "./arch/arm64/include/asm/uaccess.h" 1
	mrs x7, ttbr1_el1
// 0 "" 2
#NO_APP
	mov	x10, x6
	bfi	x10, x7, 0, 48
#APP
// 144 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr1_el1, x10
// 0 "" 2
// 145 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 148 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr0_el1, x6
// 0 "" 2
// 149 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 124 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daif, x0
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x0
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L142 - .	
		.quad		gic_pmr_sync - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L139:
	mov	w0, w11
#APP
	1:	ldtrb	w6, [x3]
2:
	.section .fixup, "ax"
	.align	2
3:	mov	w0, -14
	mov	x6, #0
	b	2b
	.previous
	.pushsection	__ex_table, "a"
	.align		3
	.long		(1b - .), (3b - .)
	.popsection

// 0 "" 2
#NO_APP
	and	w6, w6, 255
#APP
// 38 "./arch/arm64/include/asm/jump_label.h" 1
	1:	b		.L143		
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L143 - .	
		.quad		arm64_const_caps_ready+1 - .			
		.popsection				
	
// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L144 - .	
		.quad		cpu_hwcap_keys+64 - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L146:
#APP
// 72 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	mrs	x7, daif
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x7, (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5))
	.purgem	mrs_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 87 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	and	w3, w7, #0x00000080
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	eor	w3, w7, #0xe0
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	cbz	w3, .L179
.L148:
#APP
// 117 "./arch/arm64/include/asm/uaccess.h" 1
	mrs x3, ttbr1_el1
// 0 "" 2
#NO_APP
	and	x3, x3, 281474976710655
	sub	x10, x3, #4096
#APP
// 120 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr0_el1, x10
// 0 "" 2
// 121 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 123 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr1_el1, x3
// 0 "" 2
// 124 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 124 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daif, x7
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x7
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L150 - .	
		.quad		gic_pmr_sync - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L147:
	strb	w6, [x9], 1
	cbnz	w0, .L173
	add	x1, x1, 1
	add	x4, x4, 256
	subs	x2, x2, #1
	beq	.L152
	ldr	w0, [x5, 52]
	ldr	x3, [x5, 8]
	tbz	x0, 21, .L180
.L132:
	and	x0, x1, x4, asr 8
#APP
// 79 "./arch/arm64/include/asm/uaccess.h" 1
		adds	x0, x0, 1
	csel	x3, xzr, x3, hi
	csinv	x0, x0, xzr, cc
	sbcs	xzr, x0, x3
	cset	x0, ls

// 0 "" 2
#NO_APP
	cbnz	x0, .L181
.L173:
	ldr	x21, [sp, 32]
.L158:
	mov	w0, 1
	b	.L127
	.p2align 2,,3
.L143:
	ldr	x3, [x8, #:lo12:cpu_hwcaps]
	tst	w3, 16
	beq	.L146
.L144:
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
	b	.L147
	.p2align 2,,3
.L135:
	ldr	x0, [x8, #:lo12:cpu_hwcaps]
	tst	w0, 16
	beq	.L138
.L137:
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
	b	.L139
	.p2align 2,,3
.L142:
#APP
// 132 "./arch/arm64/include/asm/irqflags.h" 1
	dsb sy
// 0 "" 2
#NO_APP
	b	.L139
	.p2align 2,,3
.L150:
#APP
// 132 "./arch/arm64/include/asm/irqflags.h" 1
	dsb sy
// 0 "" 2
#NO_APP
	b	.L147
	.p2align 2,,3
.L154:
	cmp	w19, 8
	bne	.L174
	ldr	x1, [sp, 48]
	str	x1, [x21]
	mov	w0, 0
	ldr	x21, [sp, 32]
	b	.L127
	.p2align 2,,3
.L178:
#APP
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L162 - .	
		.quad		gic_nonsecure_priorities - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	mov	x6, 96
.L141:
#APP
// 56 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daifset, #2		// arch_local_irq_disable
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x6
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	b	.L140
	.p2align 2,,3
.L179:
#APP
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L163 - .	
		.quad		gic_nonsecure_priorities - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	mov	x3, 96
.L149:
#APP
// 56 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daifset, #2		// arch_local_irq_disable
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x3
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	b	.L148
	.p2align 2,,3
.L162:
	mov	x6, 160
	b	.L141
	.p2align 2,,3
.L163:
	mov	x3, 160
	b	.L149
	.p2align 2,,3
.L174:
	mov	w0, 1
	ldr	x21, [sp, 32]
	b	.L127
	.p2align 2,,3
.L153:
	ldr	w1, [sp, 48]
	mov	w0, 0
	str	x1, [x21]
	ldr	x21, [sp, 32]
	b	.L127
	.p2align 2,,3
.L155:
	ldrb	w1, [sp, 48]
	mov	w0, 0
	str	x1, [x21]
	ldr	x21, [sp, 32]
	b	.L127
.L177:
	str	x21, [sp, 32]
	bl	__stack_chk_fail
	.size	rk_align_rd, .-rk_align_rd
	.align	2
	.p2align 3,,7
	.global	alignment_fixup_helper
	.type	alignment_fixup_helper, %function
alignment_fixup_helper:
	hint	25 // paciasp
	stp	x29, x30, [sp, -176]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x20, x2
	mrs	x19, sp_el0
	stp	x21, x22, [sp, 32]
	mov	x22, x0
	ldr	x1, [x20, 264]
	ldr	x2, [x19, 1368]
	str	x2, [sp, 168]
	mov	x2,0
	tst	x1, 15
	bne	.L183
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x0, sp_el0
// 0 "" 2
#NO_APP
	ldr	x0, [x0]
	mov	x1, 4294963200
	tst	w0, 4194304
	bne	.L184
	adrp	x0, vabits_actual
	mov	x1, 1
	ldr	x0, [x0, #:lo12:vabits_actual]
	lsl	x1, x1, x0
.L184:
	cmp	x22, x1
	bcc	.L185
.L189:
	mov	w0, 1
.L182:
	add	x19, x19, 1368
	ldr	x1, [sp, 168]
	ldr	x2, [x19]
	subs	x1, x1, x2
	mov	x2, 0
	bne	.L442
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 176
	hint	29 // autiasp
	ret
	.p2align 2,,3
.L185:
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x1, sp_el0
// 0 "" 2
#NO_APP
	ldr	w3, [x1, 52]
	ldr	x0, [x1, 8]
	ldr	x2, [x20, 256]
	tbz	x3, 21, .L443
.L187:
	sbfx	x1, x2, 0, 56
	and	x1, x1, x2
.L188:
#APP
// 79 "./arch/arm64/include/asm/uaccess.h" 1
		adds	x1, x1, 4
	csel	x0, xzr, x0, hi
	csinv	x1, x1, xzr, cc
	sbcs	xzr, x1, x0
	cset	x1, ls

// 0 "" 2
#NO_APP
	cbz	x1, .L189
	sbfx	x0, x2, 0, 56
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x1, sp_el0
// 0 "" 2
#NO_APP
	and	x0, x0, x2
	ldr	x3, [x1, 8]
#APP
// 289 "./arch/arm64/include/asm/uaccess.h" 1
		bics	xzr, x0, x3
	csel	x1, x2, xzr, eq

// 0 "" 2
// 297 "./arch/arm64/include/asm/uaccess.h" 1
	hint #20
// 0 "" 2
// 38 "./arch/arm64/include/asm/jump_label.h" 1
	1:	b		.L190		
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L190 - .	
		.quad		arm64_const_caps_ready+1 - .			
		.popsection				
	
// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L192 - .	
		.quad		cpu_hwcap_keys+64 - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L193:
#APP
// 72 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	mrs	x0, daif
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x0, (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5))
	.purgem	mrs_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 87 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	and	w2, w0, #0x00000080
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	eor	w2, w0, #0xe0
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	cbz	w2, .L444
.L195:
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x2, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x2, 16]
#APP
// 141 "./arch/arm64/include/asm/uaccess.h" 1
	mrs x3, ttbr1_el1
// 0 "" 2
#NO_APP
	mov	x4, x2
	bfi	x4, x3, 0, 48
#APP
// 144 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr1_el1, x4
// 0 "" 2
// 145 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 148 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr0_el1, x2
// 0 "" 2
// 149 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 124 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daif, x0
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x0
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L197 - .	
		.quad		gic_pmr_sync - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	b	.L194
	.p2align 2,,3
.L183:
	ldr	x0, [x20, 256]
	add	x1, sp, 132
	bl	aarch64_insn_read
	cbnz	w0, .L189
	ldr	w0, [sp, 132]
.L206:
	bl	aarch64_get_insn_class
	cmp	w0, 4
	beq	.L207
	cmp	w0, 5
	mov	w0, 1
	bne	.L182
	ldr	w1, [sp, 132]
	mov	w2, 29728
	movk	w2, 0xd50b, lsl 16
	and	w1, w1, -32
	cmp	w1, w2
	bne	.L189
#APP
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x21, (((3) << 19) | ((3) << 16) | ((0) << 12) | ((0) << 8) | ((7) << 5))
	.purgem	mrs_s

// 0 "" 2
#NO_APP
	and	w21, w21, 15
	mov	x1, 4294963200
	add	w21, w21, 2
#APP
// 19 "./arch/arm64/include/asm/current.h" 1
	mrs x2, sp_el0
// 0 "" 2
#NO_APP
	ldr	x2, [x2]
	lsl	w21, w0, w21
	neg	w0, w21
	sxtw	x0, w0
	tst	w2, 4194304
	and	x22, x0, x22
	bne	.L208
	adrp	x0, vabits_actual
	mov	x1, 1
	ldr	x0, [x0, #:lo12:vabits_actual]
	lsl	x1, x1, x0
.L208:
	cmp	x22, x1
	bcs	.L209
	.p2align 3,,7
.L210:
	mov	x0, x22
	mov	x2, 0
	mov	w1, 1
	bl	rk_align_wr
	cbnz	w0, .L189
	subs	w21, w21, #1
	bne	.L210
.L211:
	ldr	x1, [x20, 256]
	mov	w0, 0
	add	x1, x1, 4
	str	x1, [x20, 256]
	b	.L182
	.p2align 2,,3
.L443:
	ldr	x3, [x1]
	mov	x1, x2
	tst	w3, 67108864
	beq	.L188
	b	.L187
	.p2align 2,,3
.L207:
	ldr	w21, [sp, 132]
	stp	x23, x24, [sp, 48]
	uxtw	x0, w21
	ubfx	x22, x21, 28, 2
	ubfx	x23, x21, 26, 1
	cmp	w22, 2
	beq	.L445
	cmp	w22, 3
	beq	.L446
	ldp	x23, x24, [sp, 48]
	b	.L189
	.p2align 2,,3
.L190:
	adrp	x0, cpu_hwcaps
	ldr	x0, [x0, #:lo12:cpu_hwcaps]
	tst	w0, 16
	beq	.L193
.L192:
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
.L194:
	mov	w0, 0
#APP
	1:	ldtr	w2, [x1]
2:
	.section .fixup, "ax"
	.align	2
3:	mov	w0, -14
	mov	x2, #0
	b	2b
	.previous
	.pushsection	__ex_table, "a"
	.align		3
	.long		(1b - .), (3b - .)
	.popsection

// 0 "" 2
// 38 "./arch/arm64/include/asm/jump_label.h" 1
	1:	b		.L198		
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L198 - .	
		.quad		arm64_const_caps_ready+1 - .			
		.popsection				
	
// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L199 - .	
		.quad		cpu_hwcap_keys+64 - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L201:
#APP
// 72 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	mrs	x3, daif
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	mrs_s, rt, sreg
.inst (0xd5200000|(\sreg)|(.L__reg_num_\rt))
		.endm
	mrs_s x3, (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5))
	.purgem	mrs_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 87 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	and	w1, w3, #0x00000080
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
	eor	w1, w3, #0xe0
664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	cbz	w1, .L447
.L203:
#APP
// 117 "./arch/arm64/include/asm/uaccess.h" 1
	mrs x1, ttbr1_el1
// 0 "" 2
#NO_APP
	and	x1, x1, 281474976710655
	sub	x4, x1, #4096
#APP
// 120 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr0_el1, x4
// 0 "" 2
// 121 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 123 "./arch/arm64/include/asm/uaccess.h" 1
	msr ttbr1_el1, x1
// 0 "" 2
// 124 "./arch/arm64/include/asm/uaccess.h" 1
	isb
// 0 "" 2
// 124 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daif, x3
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x3
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L205 - .	
		.quad		gic_pmr_sync - .			
		.popsection				
	
// 0 "" 2
#NO_APP
.L202:
	cbnz	w0, .L189
	mov	w0, w2
	str	w2, [sp, 132]
	b	.L206
	.p2align 2,,3
.L198:
	adrp	x1, cpu_hwcaps
	ldr	x1, [x1, #:lo12:cpu_hwcaps]
	tst	w1, 16
	beq	.L201
.L199:
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
	b	.L202
	.p2align 2,,3
.L197:
#APP
// 132 "./arch/arm64/include/asm/irqflags.h" 1
	dsb sy
// 0 "" 2
#NO_APP
	b	.L194
	.p2align 2,,3
.L205:
#APP
// 132 "./arch/arm64/include/asm/irqflags.h" 1
	dsb sy
// 0 "" 2
#NO_APP
	b	.L202
	.p2align 2,,3
.L446:
	stp	x25, x26, [sp, 64]
	ubfx	x24, x0, 10, 2
	tbnz	x0, 24, .L252
	cmp	x24, 2
	ubfx	x0, x0, 16, 6
	cset	w25, ne
	ands	w0, w0, 32
	csel	w25, w25, wzr, eq
	cbnz	w25, .L252
	cmp	w0, 0
	ccmp	x24, 2, 0, ne
	beq	.L448
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	b	.L189
	.p2align 2,,3
.L445:
	and	w0, w21, 16777216
	stp	x25, x26, [sp, 64]
	lsr	w24, w21, 30
	stp	x27, x28, [sp, 80]
	and	w26, w21, 8388608
	ubfx	x25, x21, 22, 1
	str	w0, [sp, 108]
	mov	w1, w21
	mov	w0, 1
	cbnz	x23, .L213
	bl	aarch64_insn_decode_register
	mov	w27, w0
	mov	w1, w21
	mov	w0, 0
	bl	aarch64_insn_decode_register
	mov	w28, w0
	mov	w1, w21
	mov	w0, w22
	bl	aarch64_insn_decode_register
	mov	w22, w0
	mov	w1, w21
	mov	w0, 7
	bl	aarch64_insn_decode_immediate
	eor	w1, w25, 1
	and	w7, w24, 1
	tst	w24, w1
	mov	w5, w1
	bne	.L320
	cmp	w24, 3
	beq	.L320
	cbz	w26, .L215
	cmp	w27, w28
	ccmp	w27, w22, 4, ne
	ccmp	w27, 31, 4, eq
	bne	.L320
.L215:
	cmp	w28, w22
	csel	w2, w25, wzr, eq
	cbnz	w2, .L320
	asr	w4, w24, 1
	mov	w6, 8
	add	w4, w4, 2
	and	w2, w27, 536870911
	cmp	w2, 32
	lsl	w6, w6, w4
	asr	w1, w6, 3
	sxtw	x24, w1
	beq	.L216
	bhi	.L217
	cmp	w2, 30
	bhi	.L449
	ldr	x25, [x20, x2, lsl 3]
.L220:
	sbfx	x21, x0, 0, 7
	ldr	w0, [sp, 108]
	lsl	x21, x21, x4
	cmp	w0, 0
	add	x0, x25, x21
	csel	x25, x0, x25, ne
	cbz	w5, .L223
	cmp	w28, 31
	mov	x2, 0
	beq	.L224
	ldr	x2, [x20, w28, sxtw 3]
.L224:
	str	x2, [sp, 152]
	cmp	w22, 31
	beq	.L225
	ldr	x23, [x20, w22, sxtw 3]
.L225:
	mov	x0, x25
	str	w1, [sp, 112]
	str	x23, [sp, 160]
	bl	rk_align_wr
	ldr	w1, [sp, 112]
	cbnz	w0, .L320
	ldr	x2, [sp, 160]
	add	x0, x24, x25
	bl	rk_align_wr
	cbnz	w0, .L320
.L227:
	cbz	w26, .L435
	ldr	w0, [sp, 108]
	cbnz	w0, .L232
	add	x25, x25, x21
.L232:
	cmp	w27, 31
	beq	.L450
	str	x25, [x20, w27, sxtw 3]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
.L460:
	cbnz	x24, .L320
	tbz	x25, 0, .L435
	.p2align 3,,7
.L320:
	mov	w0, 1
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L182
	.p2align 2,,3
.L447:
#APP
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L339 - .	
		.quad		gic_nonsecure_priorities - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	mov	x1, 96
.L204:
#APP
// 56 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daifset, #2		// arch_local_irq_disable
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x1
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	b	.L203
	.p2align 2,,3
.L444:
#APP
// 21 "./arch/arm64/include/asm/jump_label.h" 1
	1:	nop					
		.pushsection	__jump_table, "aw"	
		.align		3			
		.long		1b - ., .L338 - .	
		.quad		gic_nonsecure_priorities - .			
		.popsection				
	
// 0 "" 2
#NO_APP
	mov	x2, 96
.L196:
#APP
// 56 "./arch/arm64/include/asm/irqflags.h" 1
	.if 1 == 1
661:
	msr	daifset, #2		// arch_local_irq_disable
662:
.pushsection .altinstructions,"a"
 .word 661b - .
 .word 663f - .
 .hword 42
 .byte 662b-661b
 .byte 664f-663f
.popsection
.subsection 1
663:
		.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__reg_num_x\num, \num
	.endr
	.equ	.L__reg_num_xzr, 31
	.macro	msr_s, sreg, rt
.inst (0xd5000000|(\sreg)|(.L__reg_num_\rt))
		.endm
	msr_s (((3) << 19) | ((0) << 16) | ((4) << 12) | ((6) << 8) | ((0) << 5)), x2
	.purgem	msr_s

664:
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.previous
.endif

// 0 "" 2
#NO_APP
	b	.L195
	.p2align 2,,3
.L213:
	bl	aarch64_insn_decode_register
	mov	w27, w0
	mov	w1, w21
	mov	w0, 0
	bl	aarch64_insn_decode_register
	mov	w23, w0
	mov	w1, w21
	mov	w0, w22
	bl	aarch64_insn_decode_register
	mov	w22, w0
	mov	w1, w21
	mov	w0, 7
	bl	aarch64_insn_decode_immediate
	cmp	w24, 3
	beq	.L320
	cmp	w23, w22
	csel	w1, w25, wzr, eq
	cbnz	w1, .L320
	add	w24, w24, 2
	mov	w3, 8
	and	w1, w27, 536870911
	lsl	w3, w3, w24
	cmp	w1, 32
	asr	w3, w3, 3
	sxtw	x2, w3
	str	x2, [sp, 112]
	beq	.L235
	bhi	.L236
	cmp	w1, 30
	bhi	.L451
	ldr	x28, [x20, x1, lsl 3]
.L239:
	sbfx	x0, x0, 0, 7
	ldr	w1, [sp, 108]
	lsl	x21, x0, x24
	add	x0, x28, x21
	cmp	w1, 0
	csel	x28, x0, x28, ne
	cbnz	w25, .L242
	mov	w1, 0
	mov	w0, w23
	str	w3, [sp, 120]
	bl	get_data
	mov	x5, x0
	mov	w1, 0
	mov	w0, w22
	str	x5, [sp, 136]
	bl	get_data
	str	x0, [sp, 152]
	ldr	w3, [sp, 120]
	cmp	w24, 4
	beq	.L452
	mov	x2, x5
	mov	w1, w3
	mov	x0, x28
	str	w3, [sp, 120]
	bl	rk_align_wr
	cbnz	w0, .L320
	ldr	w3, [sp, 120]
	ldr	x2, [sp, 152]
	mov	w1, w3
	ldr	x0, [sp, 112]
	add	x0, x0, x28
	bl	rk_align_wr
	cbnz	w0, .L320
.L245:
	cbz	w26, .L435
	ldr	w0, [sp, 108]
	cbnz	w0, .L250
	add	x28, x28, x21
.L250:
	cmp	w27, 31
	beq	.L453
	str	x28, [x20, w27, sxtw 3]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
	.p2align 2,,3
.L252:
	and	w22, w21, 33553408
	stp	x27, x28, [sp, 80]
	and	w22, w22, -16775169
	lsr	w28, w21, 30
	lsr	w25, w21, 22
	cmp	w22, 1024
	eor	x2, x21, 2048
	cbnz	x23, .L254
	ubfx	x2, x2, 11, 1
	mov	w1, w21
	csel	w2, w2, wzr, eq
	mov	w0, 1
	str	w2, [sp, 108]
	bl	aarch64_insn_decode_register
	mov	w27, w0
	mov	w1, w21
	mov	w0, 0
	bl	aarch64_insn_decode_register
	mov	w26, w0
	mov	w1, w21
	tbnz	x21, 24, .L256
	mov	w0, 6
	bl	aarch64_insn_decode_immediate
	sbfx	x21, x0, 0, 9
.L257:
	tbnz	x25, 1, .L258
	mvn	w0, w25
	cmp	w28, 3
	mov	w25, 64
	mov	w1, 32
	and	w0, w0, 1
	csel	w25, w25, w1, eq
	mov	w4, 0
.L259:
	cmp	w27, w26
	ccmp	w27, 31, 4, eq
	bne	.L320
	and	w1, w27, 536870911
	cmp	w1, 32
	beq	.L263
	bhi	.L264
	cmp	w1, 30
	bhi	.L454
	ldr	x24, [x20, x1, lsl 3]
.L267:
	mov	w1, 8
	ldr	w2, [sp, 108]
	lsl	w28, w1, w28
	asr	w1, w28, 3
	cmp	w2, 0
	add	x2, x24, x21
	csel	x24, x2, x24, eq
	cbz	w0, .L270
	cmp	w26, 31
	beq	.L271
	ldr	x23, [x20, w26, sxtw 3]
.L271:
	mov	x2, x23
	mov	x0, x24
	str	x23, [sp, 160]
	bl	rk_align_wr
	cbnz	w0, .L320
.L272:
	cmp	w22, 1024
	beq	.L455
.L435:
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
	.p2align 2,,3
.L339:
	mov	x1, 160
	b	.L204
	.p2align 2,,3
.L338:
	mov	x2, 160
	b	.L196
.L209:
	sxtw	x2, w21
	mov	x0, x22
	mov	w1, 0
	bl	__memset_io
	b	.L211
.L451:
	cmp	w1, 31
	bne	.L437
	ldr	x28, [x20, 248]
	b	.L239
.L236:
	cmp	w1, 33
	bne	.L437
	ldr	x28, [x20, 264]
	b	.L239
.L254:
	ubfx	x2, x2, 11, 1
	lsl	w0, w25, 1
	and	w0, w0, 4
	csel	w24, w2, wzr, eq
	mov	w1, w21
	orr	w23, w0, w28
	mov	w0, 1
	bl	aarch64_insn_decode_register
	mov	w1, w21
	mov	w26, w0
	mov	w0, 0
	bl	aarch64_insn_decode_register
	cmp	w23, 4
	mov	w27, w0
	bhi	.L320
	mov	w1, w21
	tbnz	x21, 24, .L281
	mov	w0, 6
	bl	aarch64_insn_decode_immediate
	sbfx	x28, x0, 0, 9
.L282:
	and	w0, w26, 536870911
	mov	w1, 8
	cmp	w0, 32
	lsl	w3, w1, w23
	beq	.L283
	bhi	.L284
	cmp	w0, 30
	bhi	.L456
	ldr	x21, [x20, x0, lsl 3]
.L287:
	cmp	w24, 0
	add	x0, x21, x28
	csel	x21, x0, x21, eq
	tbnz	x25, 0, .L290
	mov	w0, w27
	mov	w1, 0
	bl	get_data
	str	x0, [sp, 152]
	mov	x4, x0
	cmp	w23, 4
	beq	.L457
	mov	x2, x0
	asr	w1, w3, 3
	mov	x0, x21
	bl	rk_align_wr
	cbnz	w0, .L320
.L293:
	cmp	w22, 1024
	bne	.L435
	cbz	w24, .L298
	add	x21, x21, x28
.L298:
	cmp	w26, 31
	beq	.L458
	str	x21, [x20, w26, sxtw 3]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
	.p2align 2,,3
.L448:
	stp	x27, x28, [sp, 80]
	ubfx	x1, x21, 13, 1
	lsr	w26, w21, 30
	lsr	w28, w21, 22
	ubfx	x0, x21, 12, 1
	cmp	w1, 0
	cbnz	x23, .L300
	mov	w1, 64
	mov	w27, 32
	csel	w27, w27, w1, eq
	cmp	w0, 0
	csel	w0, w26, wzr, ne
	mov	w1, w21
	str	w0, [sp, 108]
	mov	w0, 1
	bl	aarch64_insn_decode_register
	mov	w24, w0
	mov	w1, w21
	mov	w0, 0
	bl	aarch64_insn_decode_register
	mov	w1, w21
	mov	w21, w0
	mov	w0, w22
	bl	aarch64_insn_decode_register
	mov	w1, w0
	tbnz	x28, 1, .L303
	mvn	w25, w28
	and	w25, w25, 1
.L304:
	cmp	w24, w21
	ccmp	w24, 31, 4, eq
	bne	.L320
	cmp	w1, 31
	mov	x0, 0
	beq	.L308
	ldr	x0, [x20, w1, sxtw 3]
.L308:
	cmp	w27, 32
	and	x1, x0, 4294967295
	and	w24, w24, 536870911
	csel	x0, x1, x0, eq
	ldrb	w1, [sp, 108]
	cmp	w24, 32
	lsl	x0, x0, x1
	beq	.L310
	bhi	.L311
	cmp	w24, 30
	bhi	.L459
	ldr	x3, [x20, x24, lsl 3]
.L314:
	mov	w1, 8
	lsl	w1, w1, w26
	add	x0, x0, x3
	asr	w1, w1, 3
	cbz	w25, .L316
	cmp	w21, 31
	beq	.L317
	ldr	x23, [x20, w21, sxtw 3]
.L317:
	mov	x2, x23
	str	x23, [sp, 160]
	bl	rk_align_wr
	cbz	w0, .L435
	b	.L320
	.p2align 2,,3
.L449:
	cmp	w2, 31
	bne	.L436
	ldr	x25, [x20, 248]
	b	.L220
.L217:
	cmp	w2, 33
	bne	.L436
	ldr	x25, [x20, 264]
	b	.L220
.L456:
	cmp	w0, 31
	bne	.L439
	ldr	x21, [x20, 248]
	b	.L287
.L284:
	cmp	w0, 33
	bne	.L439
	ldr	x21, [x20, 264]
	b	.L287
.L437:
	mov	x28, 0
	b	.L239
.L258:
	cmp	w28, 3
	beq	.L460
	and	w25, w25, 1
	cmp	w28, 2
	beq	.L461
	cmp	w25, 0
	mov	w0, 32
	mov	w25, 64
	mov	w4, 1
	csel	w25, w25, w0, eq
	mov	w0, 0
	b	.L259
.L256:
	mov	w0, 5
	bl	aarch64_insn_decode_immediate
	lsl	x21, x0, x28
	b	.L257
.L242:
	add	x2, sp, 136
	cmp	w24, 4
	beq	.L462
	mov	w1, w3
	mov	x0, x28
	str	w3, [sp, 120]
	bl	rk_align_rd
	cbnz	w0, .L320
	ldr	w3, [sp, 120]
	add	x2, sp, 152
	ldr	x0, [sp, 112]
	mov	w1, w3
	add	x0, x0, x28
	bl	rk_align_rd
	cbnz	w0, .L320
	str	xzr, [sp, 144]
	str	xzr, [sp, 160]
.L248:
	ldr	x2, [sp, 136]
	mov	w0, w23
	mov	w1, 0
	bl	set_data
	ldr	x2, [sp, 144]
	mov	w0, w23
	mov	w1, 1
	bl	set_data
	ldr	x2, [sp, 152]
	mov	w0, w22
	mov	w1, 0
	bl	set_data
	ldr	x2, [sp, 160]
	mov	w0, w22
	mov	w1, 1
	bl	set_data
	b	.L245
.L300:
	mov	w1, 64
	lsl	w23, w28, 1
	mov	w25, 32
	csel	w25, w25, w1, eq
	cmp	w0, 0
	and	w23, w23, 4
	orr	w23, w23, w26
	mov	w1, w21
	csel	w26, w23, wzr, ne
	mov	w0, 1
	bl	aarch64_insn_decode_register
	mov	w24, w0
	mov	w1, w21
	mov	w0, 0
	bl	aarch64_insn_decode_register
	mov	w27, w0
	mov	w1, w21
	mov	w0, w22
	bl	aarch64_insn_decode_register
	mov	w1, w0
	tbz	x28, 1, .L320
	cmp	w24, w27
	ccmp	w24, 31, 4, eq
	bne	.L320
	cmp	w0, 31
	mov	x0, 0
	beq	.L321
	ldr	x0, [x20, w1, sxtw 3]
.L321:
	cmp	w25, 32
	and	x1, x0, 4294967295
	and	w24, w24, 536870911
	csel	x0, x1, x0, eq
	cmp	w24, 32
	lsl	x0, x0, x26
	beq	.L323
	bhi	.L324
	cmp	w24, 30
	bhi	.L463
	ldr	x21, [x20, x24, lsl 3]
.L327:
	mov	w22, 8
	add	x21, x0, x21
	lsl	w4, w22, w23
	tbnz	x28, 0, .L329
	mov	w0, w27
	mov	w1, 0
	bl	get_data
	str	x0, [sp, 152]
	mov	x3, x0
	cmp	w23, 4
	beq	.L464
	mov	x2, x0
	asr	w1, w4, 3
	mov	x0, x21
	bl	rk_align_wr
	cbz	w0, .L435
	b	.L320
	.p2align 2,,3
.L436:
	mov	x25, 0
	b	.L220
.L454:
	cmp	w1, 31
	bne	.L438
	ldr	x24, [x20, 248]
	b	.L267
.L264:
	cmp	w1, 33
	bne	.L438
	ldr	x24, [x20, 264]
	b	.L267
.L439:
	mov	x21, 0
	b	.L287
.L281:
	mov	w0, 5
	bl	aarch64_insn_decode_immediate
	lsl	x28, x0, x23
	b	.L282
.L290:
	cmp	w23, 4
	beq	.L465
	asr	w1, w3, 3
	add	x2, sp, 152
	mov	x0, x21
	bl	rk_align_rd
	cbnz	w0, .L320
	str	xzr, [sp, 160]
.L296:
	ldr	x2, [sp, 152]
	mov	w0, w27
	mov	w1, 0
	bl	set_data
	ldr	x2, [sp, 160]
	mov	w0, w27
	mov	w1, 1
	bl	set_data
	b	.L293
.L223:
	add	x2, sp, 152
	mov	x0, x25
	str	w1, [sp, 112]
	stp	w6, w7, [sp, 120]
	bl	rk_align_rd
	cbnz	w0, .L320
	ldr	w1, [sp, 112]
	add	x0, x24, x25
	add	x2, sp, 160
	bl	rk_align_rd
	cbnz	w0, .L320
	ldr	w7, [sp, 124]
	cbz	w7, .L228
	ldp	x2, x1, [sp, 152]
	mov	w0, 64
	ldr	w6, [sp, 120]
	sub	w6, w0, w6
	lsl	x2, x2, x6
	lsl	x0, x1, x6
	asr	x1, x2, x6
	asr	x6, x0, x6
	stp	x1, x6, [sp, 152]
.L228:
	cmp	w28, 31
	beq	.L229
	ldr	x0, [sp, 152]
	str	x0, [x20, w28, sxtw 3]
.L229:
	cmp	w22, 31
	beq	.L227
	ldr	x0, [sp, 160]
	str	x0, [x20, w22, sxtw 3]
	b	.L227
.L459:
	cmp	w24, 31
	bne	.L440
	ldr	x3, [x20, 248]
	b	.L314
.L311:
	cmp	w24, 33
	bne	.L440
	ldr	x3, [x20, 264]
	b	.L314
.L303:
	cmp	w26, 3
	beq	.L466
	cmp	w26, 2
	bne	.L304
	tbz	x28, 0, .L304
	b	.L320
	.p2align 2,,3
.L438:
	mov	x24, 0
	b	.L267
.L235:
	ldr	x28, [x20, 256]
	b	.L239
.L463:
	cmp	w24, 31
	bne	.L441
	ldr	x21, [x20, 248]
	b	.L327
.L324:
	cmp	w24, 33
	bne	.L441
	ldr	x21, [x20, 264]
	b	.L327
.L462:
	mov	x0, x28
	mov	w1, 8
	bl	rk_align_rd
	cbnz	w0, .L320
	add	x2, sp, 144
	add	x0, x28, 8
	mov	w1, 8
	bl	rk_align_rd
	cbnz	w0, .L320
	add	x2, sp, 152
	add	x0, x28, 16
	mov	w1, 8
	bl	rk_align_rd
	cbnz	w0, .L320
	add	x2, sp, 160
	add	x0, x28, 24
	mov	w1, 8
	bl	rk_align_rd
	cbz	w0, .L248
	b	.L320
	.p2align 2,,3
.L452:
	mov	w1, 1
	mov	w0, w23
	bl	get_data
	mov	x2, x0
	mov	w1, 1
	mov	w0, w22
	str	x2, [sp, 144]
	bl	get_data
	mov	x3, x0
	mov	x2, x5
	mov	x0, x28
	mov	w1, 8
	str	x3, [sp, 160]
	bl	rk_align_wr
	cbnz	w0, .L320
	ldr	x2, [sp, 144]
	add	x0, x28, 8
	mov	w1, 8
	bl	rk_align_wr
	cbnz	w0, .L320
	ldr	x2, [sp, 152]
	add	x0, x28, 16
	mov	w1, 8
	bl	rk_align_wr
	cbnz	w0, .L320
	ldr	x2, [sp, 160]
	add	x0, x28, 24
	mov	w1, 8
	bl	rk_align_wr
	cbz	w0, .L245
	b	.L320
	.p2align 2,,3
.L216:
	ldr	x25, [x20, 256]
	b	.L220
.L283:
	ldr	x21, [x20, 256]
	b	.L287
.L440:
	mov	x3, 0
	b	.L314
.L270:
	add	x2, sp, 160
	mov	x0, x24
	str	w4, [sp, 112]
	bl	rk_align_rd
	cbnz	w0, .L320
	ldr	w4, [sp, 112]
	cbz	w4, .L273
	sub	w28, w28, #1
	mov	w0, 31
	ldr	x1, [sp, 160]
	sub	w2, w0, w28, sxtb
	mov	w0, 63
	cmp	w25, 32
	sub	w28, w0, w28, sxtb
	lsl	w0, w1, w2
	asr	w0, w0, w2
	sxtw	x0, w0
	lsl	x1, x1, x28
	asr	x1, x1, x28
	csel	x0, x0, x1, eq
	str	x0, [sp, 160]
.L273:
	cmp	w26, 31
	beq	.L272
	ldr	x0, [sp, 160]
	str	x0, [x20, w26, sxtw 3]
	b	.L272
.L465:
	add	x2, sp, 152
	mov	x0, x21
	mov	w1, 8
	bl	rk_align_rd
	cbnz	w0, .L320
	add	x2, sp, 160
	add	x0, x21, 8
	mov	w1, 8
	bl	rk_align_rd
	cbz	w0, .L296
	b	.L320
.L457:
	mov	w1, 1
	mov	w0, w27
	bl	get_data
	mov	x3, x0
	mov	x2, x4
	mov	x0, x21
	mov	w1, 8
	str	x3, [sp, 160]
	bl	rk_align_wr
	cbnz	w0, .L320
	ldr	x2, [sp, 160]
	add	x0, x21, 8
	mov	w1, 8
	bl	rk_align_wr
	cbz	w0, .L293
	b	.L320
.L461:
	cbnz	w25, .L320
	mov	w25, 64
	mov	w4, 1
	mov	w0, 0
	b	.L259
.L329:
	cmp	w23, 4
	beq	.L467
	asr	w1, w4, 3
	mov	x0, x21
	add	x2, sp, 152
	bl	rk_align_rd
	cbnz	w0, .L320
	str	xzr, [sp, 160]
.L334:
	ldr	x2, [sp, 152]
	mov	w0, w27
	mov	w1, 0
	bl	set_data
	ldr	x2, [sp, 160]
	mov	w0, w27
	mov	w1, 1
	bl	set_data
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
.L441:
	mov	x21, 0
	b	.L327
.L455:
	ldr	w0, [sp, 108]
	cbz	w0, .L277
	add	x24, x24, x21
.L277:
	cmp	w27, 31
	beq	.L468
	str	x24, [x20, w27, sxtw 3]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
.L316:
	add	x2, sp, 160
	bl	rk_align_rd
	cbz	w0, .L435
	b	.L320
.L263:
	ldr	x24, [x20, 256]
	b	.L267
.L466:
	tbz	x28, 0, .L435
	b	.L320
.L442:
	stp	x23, x24, [sp, 48]
	stp	x25, x26, [sp, 64]
	stp	x27, x28, [sp, 80]
	bl	__stack_chk_fail
.L453:
	str	x28, [x20, 248]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
.L458:
	str	x21, [x20, 248]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
.L310:
	ldr	x3, [x20, 256]
	b	.L314
.L450:
	str	x25, [x20, 248]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
.L323:
	ldr	x21, [x20, 256]
	b	.L327
.L467:
	add	x2, sp, 152
	mov	w1, w22
	mov	x0, x21
	bl	rk_align_rd
	cbnz	w0, .L320
	mov	w1, w22
	add	x0, x21, 8
	add	x2, sp, 160
	bl	rk_align_rd
	cbz	w0, .L334
	b	.L320
.L464:
	mov	w1, 1
	mov	w0, w27
	bl	get_data
	mov	x4, x0
	mov	x2, x3
	mov	w1, w22
	mov	x0, x21
	str	x4, [sp, 160]
	bl	rk_align_wr
	cbnz	w0, .L320
	ldr	x2, [sp, 160]
	mov	w1, w22
	add	x0, x21, 8
	bl	rk_align_wr
	cbz	w0, .L435
	b	.L320
.L468:
	str	x24, [x20, 248]
	ldp	x23, x24, [sp, 48]
	ldp	x25, x26, [sp, 64]
	ldp	x27, x28, [sp, 80]
	b	.L211
	.size	alignment_fixup_helper, .-alignment_fixup_helper
	.ident	"GCC: (Ubuntu 9.4.0-1ubuntu1~20.04.1) 9.4.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align	3
	.word	4
	.word	16
	.word	5
	.string	"GNU"
	.word	3221225472
	.word	4
	.word	2
	.align	3
