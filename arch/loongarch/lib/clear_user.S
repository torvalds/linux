/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/export.h>
#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/cpu.h>
#include <asm/regdef.h>

.irp to, 0, 1, 2, 3, 4, 5, 6, 7
.L_fixup_handle_\to\():
	sub.d	a0, a2, a0
	addi.d	a0, a0, (\to) * (-8)
	jr	ra
.endr

.irp to, 0, 2, 4
.L_fixup_handle_s\to\():
	addi.d	a0, a1, -\to
	jr	ra
.endr

SYM_FUNC_START(__clear_user)
	/*
	 * Some CPUs support hardware unaligned access
	 */
	ALTERNATIVE	"b __clear_user_generic",	\
			"b __clear_user_fast", CPU_FEATURE_UAL
SYM_FUNC_END(__clear_user)

EXPORT_SYMBOL(__clear_user)

/*
 * unsigned long __clear_user_generic(void *addr, size_t size)
 *
 * a0: addr
 * a1: size
 */
SYM_FUNC_START(__clear_user_generic)
	beqz	a1, 2f

1:	st.b	zero, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, -1
	bgtz	a1, 1b

2:	move	a0, a1
	jr	ra

	_asm_extable 1b, .L_fixup_handle_s0
SYM_FUNC_END(__clear_user_generic)

/*
 * unsigned long __clear_user_fast(void *addr, unsigned long size)
 *
 * a0: addr
 * a1: size
 */
SYM_FUNC_START(__clear_user_fast)
	sltui	t0, a1, 9
	bnez	t0, .Lsmall

	add.d	a2, a0, a1
0:	st.d	zero, a0, 0

	/* align up address */
	addi.d	a0, a0, 8
	bstrins.d	a0, zero, 2, 0

	addi.d	a3, a2, -64
	bgeu	a0, a3, .Llt64

	/* set 64 bytes at a time */
.Lloop64:
1:	st.d	zero, a0, 0
2:	st.d	zero, a0, 8
3:	st.d	zero, a0, 16
4:	st.d	zero, a0, 24
5:	st.d	zero, a0, 32
6:	st.d	zero, a0, 40
7:	st.d	zero, a0, 48
8:	st.d	zero, a0, 56
	addi.d	a0, a0, 64
	bltu	a0, a3, .Lloop64

	/* set the remaining bytes */
.Llt64:
	addi.d	a3, a2, -32
	bgeu	a0, a3, .Llt32
9:	st.d	zero, a0, 0
10:	st.d	zero, a0, 8
11:	st.d	zero, a0, 16
12:	st.d	zero, a0, 24
	addi.d	a0, a0, 32

.Llt32:
	addi.d	a3, a2, -16
	bgeu	a0, a3, .Llt16
13:	st.d	zero, a0, 0
14:	st.d	zero, a0, 8
	addi.d	a0, a0, 16

.Llt16:
	addi.d	a3, a2, -8
	bgeu	a0, a3, .Llt8
15:	st.d	zero, a0, 0
	addi.d	a0, a0, 8

.Llt8:
16:	st.d	zero, a2, -8

	/* return */
	move	a0, zero
	jr	ra

	.align	4
.Lsmall:
	pcaddi	t0, 4
	slli.d	a2, a1, 4
	add.d	t0, t0, a2
	jr	t0

	.align	4
	move	a0, zero
	jr	ra

	.align	4
17:	st.b	zero, a0, 0
	move	a0, zero
	jr	ra

	.align	4
18:	st.h	zero, a0, 0
	move	a0, zero
	jr	ra

	.align	4
19:	st.h	zero, a0, 0
20:	st.b	zero, a0, 2
	move	a0, zero
	jr	ra

	.align	4
21:	st.w	zero, a0, 0
	move	a0, zero
	jr	ra

	.align	4
22:	st.w	zero, a0, 0
23:	st.b	zero, a0, 4
	move	a0, zero
	jr	ra

	.align	4
24:	st.w	zero, a0, 0
25:	st.h	zero, a0, 4
	move	a0, zero
	jr	ra

	.align	4
26:	st.w	zero, a0, 0
27:	st.w	zero, a0, 3
	move	a0, zero
	jr	ra

	.align	4
28:	st.d	zero, a0, 0
	move	a0, zero
	jr	ra

	/* fixup and ex_table */
	_asm_extable 0b, .L_fixup_handle_0
	_asm_extable 1b, .L_fixup_handle_0
	_asm_extable 2b, .L_fixup_handle_1
	_asm_extable 3b, .L_fixup_handle_2
	_asm_extable 4b, .L_fixup_handle_3
	_asm_extable 5b, .L_fixup_handle_4
	_asm_extable 6b, .L_fixup_handle_5
	_asm_extable 7b, .L_fixup_handle_6
	_asm_extable 8b, .L_fixup_handle_7
	_asm_extable 9b, .L_fixup_handle_0
	_asm_extable 10b, .L_fixup_handle_1
	_asm_extable 11b, .L_fixup_handle_2
	_asm_extable 12b, .L_fixup_handle_3
	_asm_extable 13b, .L_fixup_handle_0
	_asm_extable 14b, .L_fixup_handle_1
	_asm_extable 15b, .L_fixup_handle_0
	_asm_extable 16b, .L_fixup_handle_0
	_asm_extable 17b, .L_fixup_handle_s0
	_asm_extable 18b, .L_fixup_handle_s0
	_asm_extable 19b, .L_fixup_handle_s0
	_asm_extable 20b, .L_fixup_handle_s2
	_asm_extable 21b, .L_fixup_handle_s0
	_asm_extable 22b, .L_fixup_handle_s0
	_asm_extable 23b, .L_fixup_handle_s4
	_asm_extable 24b, .L_fixup_handle_s0
	_asm_extable 25b, .L_fixup_handle_s4
	_asm_extable 26b, .L_fixup_handle_s0
	_asm_extable 27b, .L_fixup_handle_s4
	_asm_extable 28b, .L_fixup_handle_s0
SYM_FUNC_END(__clear_user_fast)
