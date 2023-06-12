/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/cpu.h>
#include <asm/export.h>
#include <asm/regdef.h>

.irp to, 0, 1, 2, 3, 4, 5, 6, 7
.L_fixup_handle_\to\():
	sub.d	a0, a2, a0
	addi.d	a0, a0, (\to) * (-8)
	jr	ra
.endr

.irp to, 0, 2, 4
.L_fixup_handle_s\to\():
	addi.d	a0, a2, -\to
	jr	ra
.endr

SYM_FUNC_START(__copy_user)
	/*
	 * Some CPUs support hardware unaligned access
	 */
	ALTERNATIVE	"b __copy_user_generic",	\
			"b __copy_user_fast", CPU_FEATURE_UAL
SYM_FUNC_END(__copy_user)

EXPORT_SYMBOL(__copy_user)

/*
 * unsigned long __copy_user_generic(void *to, const void *from, size_t n)
 *
 * a0: to
 * a1: from
 * a2: n
 */
SYM_FUNC_START(__copy_user_generic)
	beqz	a2, 3f

1:	ld.b	t0, a1, 0
2:	st.b	t0, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, 1
	addi.d	a2, a2, -1
	bgtz	a2, 1b

3:	move	a0, a2
	jr	ra

	_asm_extable 1b, .L_fixup_handle_s0
	_asm_extable 2b, .L_fixup_handle_s0
SYM_FUNC_END(__copy_user_generic)

/*
 * unsigned long __copy_user_fast(void *to, const void *from, unsigned long n)
 *
 * a0: to
 * a1: from
 * a2: n
 */
SYM_FUNC_START(__copy_user_fast)
	sltui	t0, a2, 9
	bnez	t0, .Lsmall

	add.d	a3, a1, a2
	add.d	a2, a0, a2
0:	ld.d	t0, a1, 0
1:	st.d	t0, a0, 0

	/* align up destination address */
	andi	t1, a0, 7
	sub.d	t0, zero, t1
	addi.d	t0, t0, 8
	add.d	a1, a1, t0
	add.d	a0, a0, t0

	addi.d	a4, a3, -64
	bgeu	a1, a4, .Llt64

	/* copy 64 bytes at a time */
.Lloop64:
2:	ld.d	t0, a1, 0
3:	ld.d	t1, a1, 8
4:	ld.d	t2, a1, 16
5:	ld.d	t3, a1, 24
6:	ld.d	t4, a1, 32
7:	ld.d	t5, a1, 40
8:	ld.d	t6, a1, 48
9:	ld.d	t7, a1, 56
	addi.d	a1, a1, 64
10:	st.d	t0, a0, 0
11:	st.d	t1, a0, 8
12:	st.d	t2, a0, 16
13:	st.d	t3, a0, 24
14:	st.d	t4, a0, 32
15:	st.d	t5, a0, 40
16:	st.d	t6, a0, 48
17:	st.d	t7, a0, 56
	addi.d	a0, a0, 64
	bltu	a1, a4, .Lloop64

	/* copy the remaining bytes */
.Llt64:
	addi.d	a4, a3, -32
	bgeu	a1, a4, .Llt32
18:	ld.d	t0, a1, 0
19:	ld.d	t1, a1, 8
20:	ld.d	t2, a1, 16
21:	ld.d	t3, a1, 24
	addi.d	a1, a1, 32
22:	st.d	t0, a0, 0
23:	st.d	t1, a0, 8
24:	st.d	t2, a0, 16
25:	st.d	t3, a0, 24
	addi.d	a0, a0, 32

.Llt32:
	addi.d	a4, a3, -16
	bgeu	a1, a4, .Llt16
26:	ld.d	t0, a1, 0
27:	ld.d	t1, a1, 8
	addi.d	a1, a1, 16
28:	st.d	t0, a0, 0
29:	st.d	t1, a0, 8
	addi.d	a0, a0, 16

.Llt16:
	addi.d	a4, a3, -8
	bgeu	a1, a4, .Llt8
30:	ld.d	t0, a1, 0
31:	st.d	t0, a0, 0

.Llt8:
32:	ld.d	t0, a3, -8
33:	st.d	t0, a2, -8

	/* return */
	move	a0, zero
	jr	ra

	.align	5
.Lsmall:
	pcaddi	t0, 8
	slli.d	a3, a2, 5
	add.d	t0, t0, a3
	jr	t0

	.align	5
	move	a0, zero
	jr	ra

	.align	5
34:	ld.b	t0, a1, 0
35:	st.b	t0, a0, 0
	move	a0, zero
	jr	ra

	.align	5
36:	ld.h	t0, a1, 0
37:	st.h	t0, a0, 0
	move	a0, zero
	jr	ra

	.align	5
38:	ld.h	t0, a1, 0
39:	ld.b	t1, a1, 2
40:	st.h	t0, a0, 0
41:	st.b	t1, a0, 2
	move	a0, zero
	jr	ra

	.align	5
42:	ld.w	t0, a1, 0
43:	st.w	t0, a0, 0
	move	a0, zero
	jr	ra

	.align	5
44:	ld.w	t0, a1, 0
45:	ld.b	t1, a1, 4
46:	st.w	t0, a0, 0
47:	st.b	t1, a0, 4
	move	a0, zero
	jr	ra

	.align	5
48:	ld.w	t0, a1, 0
49:	ld.h	t1, a1, 4
50:	st.w	t0, a0, 0
51:	st.h	t1, a0, 4
	move	a0, zero
	jr	ra

	.align	5
52:	ld.w	t0, a1, 0
53:	ld.w	t1, a1, 3
54:	st.w	t0, a0, 0
55:	st.w	t1, a0, 3
	move	a0, zero
	jr	ra

	.align	5
56:	ld.d	t0, a1, 0
57:	st.d	t0, a0, 0
	move	a0, zero
	jr	ra

	/* fixup and ex_table */
	_asm_extable 0b, .L_fixup_handle_0
	_asm_extable 1b, .L_fixup_handle_0
	_asm_extable 2b, .L_fixup_handle_0
	_asm_extable 3b, .L_fixup_handle_0
	_asm_extable 4b, .L_fixup_handle_0
	_asm_extable 5b, .L_fixup_handle_0
	_asm_extable 6b, .L_fixup_handle_0
	_asm_extable 7b, .L_fixup_handle_0
	_asm_extable 8b, .L_fixup_handle_0
	_asm_extable 9b, .L_fixup_handle_0
	_asm_extable 10b, .L_fixup_handle_0
	_asm_extable 11b, .L_fixup_handle_1
	_asm_extable 12b, .L_fixup_handle_2
	_asm_extable 13b, .L_fixup_handle_3
	_asm_extable 14b, .L_fixup_handle_4
	_asm_extable 15b, .L_fixup_handle_5
	_asm_extable 16b, .L_fixup_handle_6
	_asm_extable 17b, .L_fixup_handle_7
	_asm_extable 18b, .L_fixup_handle_0
	_asm_extable 19b, .L_fixup_handle_0
	_asm_extable 20b, .L_fixup_handle_0
	_asm_extable 21b, .L_fixup_handle_0
	_asm_extable 22b, .L_fixup_handle_0
	_asm_extable 23b, .L_fixup_handle_1
	_asm_extable 24b, .L_fixup_handle_2
	_asm_extable 25b, .L_fixup_handle_3
	_asm_extable 26b, .L_fixup_handle_0
	_asm_extable 27b, .L_fixup_handle_0
	_asm_extable 28b, .L_fixup_handle_0
	_asm_extable 29b, .L_fixup_handle_1
	_asm_extable 30b, .L_fixup_handle_0
	_asm_extable 31b, .L_fixup_handle_0
	_asm_extable 32b, .L_fixup_handle_0
	_asm_extable 33b, .L_fixup_handle_1
	_asm_extable 34b, .L_fixup_handle_s0
	_asm_extable 35b, .L_fixup_handle_s0
	_asm_extable 36b, .L_fixup_handle_s0
	_asm_extable 37b, .L_fixup_handle_s0
	_asm_extable 38b, .L_fixup_handle_s0
	_asm_extable 39b, .L_fixup_handle_s0
	_asm_extable 40b, .L_fixup_handle_s0
	_asm_extable 41b, .L_fixup_handle_s2
	_asm_extable 42b, .L_fixup_handle_s0
	_asm_extable 43b, .L_fixup_handle_s0
	_asm_extable 44b, .L_fixup_handle_s0
	_asm_extable 45b, .L_fixup_handle_s0
	_asm_extable 46b, .L_fixup_handle_s0
	_asm_extable 47b, .L_fixup_handle_s4
	_asm_extable 48b, .L_fixup_handle_s0
	_asm_extable 49b, .L_fixup_handle_s0
	_asm_extable 50b, .L_fixup_handle_s0
	_asm_extable 51b, .L_fixup_handle_s4
	_asm_extable 52b, .L_fixup_handle_s0
	_asm_extable 53b, .L_fixup_handle_s0
	_asm_extable 54b, .L_fixup_handle_s0
	_asm_extable 55b, .L_fixup_handle_s4
	_asm_extable 56b, .L_fixup_handle_s0
	_asm_extable 57b, .L_fixup_handle_s0
SYM_FUNC_END(__copy_user_fast)
