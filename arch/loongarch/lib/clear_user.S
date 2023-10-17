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

	_asm_extable 1b, 2b
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
.Llarge_fixup:
	sub.d	a1, a2, a0

.Lsmall_fixup:
29:	st.b	zero, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, -1
	bgt	a1, zero, 29b

.Lexit:
	move	a0, a1
	jr	ra

	_asm_extable 0b, .Lsmall_fixup
	_asm_extable 1b, .Llarge_fixup
	_asm_extable 2b, .Llarge_fixup
	_asm_extable 3b, .Llarge_fixup
	_asm_extable 4b, .Llarge_fixup
	_asm_extable 5b, .Llarge_fixup
	_asm_extable 6b, .Llarge_fixup
	_asm_extable 7b, .Llarge_fixup
	_asm_extable 8b, .Llarge_fixup
	_asm_extable 9b, .Llarge_fixup
	_asm_extable 10b, .Llarge_fixup
	_asm_extable 11b, .Llarge_fixup
	_asm_extable 12b, .Llarge_fixup
	_asm_extable 13b, .Llarge_fixup
	_asm_extable 14b, .Llarge_fixup
	_asm_extable 15b, .Llarge_fixup
	_asm_extable 16b, .Llarge_fixup
	_asm_extable 17b, .Lexit
	_asm_extable 18b, .Lsmall_fixup
	_asm_extable 19b, .Lsmall_fixup
	_asm_extable 20b, .Lsmall_fixup
	_asm_extable 21b, .Lsmall_fixup
	_asm_extable 22b, .Lsmall_fixup
	_asm_extable 23b, .Lsmall_fixup
	_asm_extable 24b, .Lsmall_fixup
	_asm_extable 25b, .Lsmall_fixup
	_asm_extable 26b, .Lsmall_fixup
	_asm_extable 27b, .Lsmall_fixup
	_asm_extable 28b, .Lsmall_fixup
	_asm_extable 29b, .Lexit
SYM_FUNC_END(__clear_user_fast)
